# Opens a tool to draw boxes around flames in images.
# Saves the annotations as YOLO-format text files.
from pathlib import Path
import argparse
import os
import tkinter as tk
from tkinter import ttk, messagebox, filedialog


def encode(boxes, width, height):
    return ''.join(f'0 {(x1+x2)/2/width:.8f} {(y1+y2)/2/height:.8f} '
                   f'{(x2-x1)/width:.8f} {(y2-y1)/height:.8f}\n'
                   for x1, y1, x2, y2 in boxes)


def decode(text, width, height):
    boxes = []
    for line in text.splitlines():
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 5 or fields[0] != '0':
            raise ValueError('O label deve ter classe 0 e quatro coordenadas por linha.')
        x, y, w, h = map(float, fields[1:])
        if not all(0 <= v <= 1 for v in (x, y, w, h)) or w <= 0 or h <= 0:
            raise ValueError('Coordenadas invalidas no label.')
        boxes.append(((x-w/2)*width, (y-h/2)*height,
                      (x+w/2)*width, (y+h/2)*height))
    return boxes


class Annotator:
    def __init__(self, root, folder):
        from PIL import Image, ImageTk
        self.Image, self.ImageTk = Image, ImageTk
        self.root, self.folder = root, folder
        self.files = sorted(p for p in folder.iterdir()
                            if p.is_file() and p.suffix.lower() in ('.jpg', '.jpeg', '.png'))
        if not self.files:
            raise ValueError('Nao encontrei fotografias diretamente nesta pasta.')
        if len({p.stem.casefold() for p in self.files}) != len(self.files):
            raise ValueError('Existem imagens com o mesmo nome base. Renomeia-as primeiro.')
        if folder.name == 'images' and folder.parent.name in ('train', 'valid'):
            self.labels = folder.parent / 'labels'
        else:
            self.labels = folder / 'labels'
        self.labels.mkdir(exist_ok=True)
        self.index = next((i for i,p in enumerate(self.files)
                           if not (self.labels / (p.stem+'.txt')).exists()), 0)
        self.boxes, self.dirty, self.start = [], False, None
        root.title('Anotar chamas')
        root.geometry('1100x850')
        root.minsize(850, 650)
        self.heading = ttk.Label(root, font=('Segoe UI', 12, 'bold'))
        self.heading.pack(pady=10)
        ttk.Label(root, text='Arrasta com o rato em volta de CADA chama. Podes desenhar varias caixas.').pack()
        self.canvas = tk.Canvas(root, background='#20242a', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, padx=12, pady=10)
        self.status = ttk.Label(root)
        self.status.pack(pady=5)
        buttons = ttk.Frame(root)
        buttons.pack(pady=10)
        for text, action in [('Anterior', lambda:self.navigate(-1)),
                             ('Desfazer caixa (Ctrl+Z)', self.undo),
                             ('Limpar caixas', self.clear),
                             ('Guardar e seguinte (Enter)', self.save),
                             ('Sem chama (N)', self.no_flame),
                             ('Saltar', lambda:self.navigate(1))]:
            ttk.Button(buttons, text=text, command=action).pack(side='left', padx=3)
        ttk.Label(root, text=f'Labels: {self.labels}', wraplength=1000).pack(pady=(0,10))
        self.canvas.bind('<Configure>', lambda e:self.redraw())
        self.canvas.bind('<ButtonPress-1>', self.press)
        self.canvas.bind('<B1-Motion>', self.drag)
        self.canvas.bind('<ButtonRelease-1>', self.release)
        root.bind('<Return>', lambda e:self.save())
        root.bind('<Control-z>', lambda e:self.undo())
        root.bind('n', lambda e:self.no_flame())
        root.bind('N', lambda e:self.no_flame())
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.load()

    def label_path(self):
        return self.labels / (self.files[self.index].stem + '.txt')

    def load(self):
        with self.Image.open(self.files[self.index]) as img:
            self.picture = img.convert('RGB')
        self.width, self.height = self.picture.size
        label = self.label_path()
        self.boxes = decode(label.read_text(), self.width, self.height) if label.exists() else []
        self.dirty, self.start = False, None
        self.redraw()

    def redraw(self):
        if not hasattr(self, 'picture'):
            return
        cw, ch = max(1,self.canvas.winfo_width()), max(1,self.canvas.winfo_height())
        self.scale = min(cw/self.width, ch/self.height)
        dw, dh = max(1,round(self.width*self.scale)), max(1,round(self.height*self.scale))
        self.ox, self.oy = (cw-dw)/2, (ch-dh)/2
        self.photo = self.ImageTk.PhotoImage(self.picture.resize((dw,dh)))
        self.canvas.delete('all')
        self.canvas.create_image(self.ox,self.oy, image=self.photo,anchor='nw')
        for number,(x1,y1,x2,y2) in enumerate(self.boxes,1):
            left,top = self.ox+x1*self.scale,self.oy+y1*self.scale
            self.canvas.create_rectangle(left,top,self.ox+x2*self.scale,self.oy+y2*self.scale,
                                         outline='#40ff70',width=2)
            self.canvas.create_text(left+4,top+4,text=str(number),anchor='nw',fill='#40ff70')
        saved=sum((self.labels/(p.stem+'.txt')).exists() for p in self.files)
        state='Alteracoes por guardar' if self.dirty else ('Anotada' if self.label_path().exists() else 'Por anotar')
        self.heading.config(text=f'{self.index+1}/{len(self.files)} — {self.files[self.index].name}')
        self.status.config(text=f'{state} | {len(self.boxes)} caixas | {saved}/{len(self.files)} imagens anotadas')

    def point(self,event):
        return (min(self.width,max(0,(event.x-self.ox)/self.scale)),
                min(self.height,max(0,(event.y-self.oy)/self.scale)))

    def press(self,event):
        if not (self.ox <= event.x <= self.ox+self.width*self.scale and
                self.oy <= event.y <= self.oy+self.height*self.scale):
            return
        self.start=self.point(event)

    def drag(self,event):
        if self.start is None:
            return
        self.canvas.delete('draft')
        x,y=self.point(event)
        sx,sy=self.start
        self.canvas.create_rectangle(self.ox+sx*self.scale,self.oy+sy*self.scale,
                                     self.ox+x*self.scale,self.oy+y*self.scale,
                                     outline='#ffd34d',width=2,tags='draft')

    def release(self,event):
        if self.start is None:
            return
        x,y=self.point(event)
        sx,sy=self.start
        self.start=None
        if abs(x-sx)>=2 and abs(y-sy)>=2:
            self.boxes.append((min(x,sx),min(y,sy),max(x,sx),max(y,sy)))
            self.dirty=True
        self.redraw()

    def undo(self):
        if self.boxes:
            self.boxes.pop()
            self.dirty=True
            self.redraw()

    def clear(self):
        if self.boxes and messagebox.askyesno('Limpar','Retirar todas as caixas desta imagem?'):
            self.boxes=[]
            self.dirty=True
            self.redraw()

    def save(self):
        if not self.boxes:
            messagebox.showinfo('Sem caixas','Se nao ha chama, usa o botao "Sem chama". Para deixar por anotar, usa "Saltar".')
            return
        self.write_label()

    def no_flame(self):
        if self.boxes and not messagebox.askyesno('Sem chama','Apagar as caixas e marcar esta imagem SEM chama?'):
            return
        self.boxes=[]
        self.dirty=True
        self.write_label()

    def write_label(self):
        destination=self.label_path()
        temporary=destination.with_suffix('.txt.tmp')
        try:
            temporary.write_text(encode(self.boxes,self.width,self.height),encoding='utf-8')
            os.replace(temporary,destination)
        except OSError as exc:
            messagebox.showerror('Nao foi possivel guardar',str(exc))
            return
        self.dirty=False
        if self.index<len(self.files)-1:
            self.index+=1
            self.load()
        else:
            self.redraw()
            messagebox.showinfo('Guardado','Ultima imagem guardada. O contador mostra se ficaram imagens por anotar.')

    def can_leave(self):
        return not self.dirty or messagebox.askyesno('Alteracoes por guardar','Descartar as alteracoes desta imagem?')

    def navigate(self,delta):
        target=self.index+delta
        if 0<=target<len(self.files) and self.can_leave():
            self.index=target
            self.load()

    def close(self):
        if self.can_leave():
            self.root.destroy()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder',nargs='?',type=Path,default=None)
    args=parser.parse_args()
    root=tk.Tk()
    folder=args.folder
    if folder is None or not folder.is_dir():
        selected=filedialog.askdirectory(title='Escolhe a pasta de imagens')
        if not selected:
            root.destroy()
            return
        folder=Path(selected)
    try:
        app=Annotator(root,folder)
    except ImportError:
        messagebox.showerror('Falta Pillow','Executa no PowerShell: python -m pip install pillow')
        root.destroy()
        return
    except Exception as exc:
        messagebox.showerror('Nao foi possivel abrir',str(exc))
        root.destroy()
        return
    root.mainloop()


if __name__=='__main__':
    main()
