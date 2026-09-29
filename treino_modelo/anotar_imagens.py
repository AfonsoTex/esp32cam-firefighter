# Opens a tool to draw boxes around flames in images.
# Saves the annotations as YOLO-format text files.
# After confirmation, copies images and labels into the dataset's train/valid folders.
# Verifies every copy before deleting the session frames and its unchanged source video.
# No manual file copying is needed; run treinar.py after finishing the session.
from pathlib import Path
import argparse
import os
import json
import math
import shutil
import uuid
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog

from extrair_frames import extract_frames, file_hash


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
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (x, y, w, h)) or w <= 0 or h <= 0:
            raise ValueError('Coordenadas invalidas no label.')
        boxes.append(((x-w/2)*width, (y-h/2)*height,
                      (x+w/2)*width, (y+h/2)*height))
    return boxes


DATASET_DIR = Path(__file__).resolve().parent.parent / "Datasets" / "Detecao_fogo"


def prepare_export(folder, train_percent=80, dataset=None):
    # Persist the exact file list so an interrupted export can be resumed.
    from PIL import Image

    folder = Path(folder).resolve(strict=True)
    if dataset is None:
        dataset = DATASET_DIR
    dataset = Path(dataset).resolve()
    if folder.is_relative_to(dataset) or dataset.is_relative_to(folder):
        raise ValueError("Escolhe uma pasta de recolha fora do dataset.")
    plan_path = folder / "export_plan.json"
    if plan_path.exists():
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        if plan["folder"] != str(folder) or plan["dataset"] != str(dataset):
            raise ValueError("O destino desta sessao mudou. Reabre no local original.")
        return plan
    if not 1 <= train_percent <= 99:
        raise ValueError("A percentagem de treino deve estar entre 1 e 99.")
    files = sorted(p for p in folder.iterdir()
                   if p.suffix.lower() in (".jpg", ".jpeg", ".png"))
    if len(files) < 2:
        raise ValueError("Sao precisas pelo menos duas imagens para dividir treino e validacao.")
    if len({p.stem.casefold() for p in files}) != len(files):
        raise ValueError("Existem imagens com nomes base repetidos.")
    metadata = {}
    if (folder / "session.json").exists():
        metadata = json.loads((folder / "session.json").read_text(encoding="utf-8"))
        if not metadata.get("complete"):
            raise ValueError("A extracao desta sessao nao terminou.")
        if "frames" in metadata and metadata["frames"] != len(files):
            raise ValueError("O numero de imagens mudou desde a extracao. O video sera preservado.")
    session_id = uuid.uuid4().hex
    train_count = max(1, min(len(files) - 1, round(len(files) * train_percent / 100)))
    entries = []
    for index, image in enumerate(files):
        label = folder / "labels" / (image.stem + ".txt")
        if image.is_symlink() or label.is_symlink() or (folder / "labels").is_symlink():
            raise ValueError("A sessao nao pode conter ligacoes para outros ficheiros.")
        if not label.is_file():
            raise ValueError(f"Falta anotar: {image.name}")
        with Image.open(image) as picture:
            decode(label.read_text(encoding="utf-8"), *picture.size)
            picture.verify()
        split = "valid"
        if index < train_count:
            split = "train"
        entries.append({"image": image.name, "label": label.name, "split": split,
                        "image_hash": file_hash(image), "label_hash": file_hash(label)})
    plan = {"id": session_id, "folder": str(folder), "dataset": str(dataset),
            "train": train_count, "valid": len(files) - train_count,
            "video": metadata.get("video"), "video_sha256": metadata.get("video_sha256"),
            "entries": entries}
    return plan


def finish_export(plan):
    folder = Path(plan["folder"]).resolve(strict=True)
    dataset = Path(plan["dataset"]).resolve()
    if folder.is_relative_to(dataset) or dataset.is_relative_to(folder):
        raise ValueError("A pasta da sessao e o dataset devem estar separados.")
    session_id = str(uuid.UUID(plan["id"]).hex)
    plan_path = folder / "export_plan.json"
    serialized = json.dumps(plan, indent=2)
    if plan_path.exists():
        if json.loads(plan_path.read_text(encoding="utf-8")) != plan:
            raise ValueError("O plano de exportacao mudou.")
    else:
        with plan_path.open("x", encoding="utf-8") as target:
            target.write(serialized)
    pairs = []
    for entry in plan["entries"]:
        if entry["split"] not in ("train", "valid"):
            raise ValueError("Divisao de dataset invalida.")
        for kind, subfolder in [("image", "images"), ("label", "labels")]:
            name = entry[kind]
            if Path(name).name != name or name in (".", ".."):
                raise ValueError("Nome de ficheiro invalido.")
            source = folder / name
            if kind == "label":
                source = folder / "labels" / name
            if source.is_symlink() or source.parent.is_symlink():
                raise ValueError("A origem nao pode ser uma ligacao.")
            destination = dataset / entry["split"] / subfolder / (session_id + "_" + name)
            if not destination.resolve().is_relative_to(dataset):
                raise ValueError("Destino fora do dataset.")
            expected = entry[kind + "_hash"]
            if source.exists() and file_hash(source) != expected:
                raise ValueError(f"Ficheiro alterado desde a confirmacao: {source}")
            if destination.exists():
                if destination.is_symlink() or file_hash(destination) != expected:
                    raise ValueError(f"Destino existente com conteudo diferente: {destination}")
            elif not source.is_file():
                raise ValueError(f"Ficheiro em falta: {source}")
            pairs.append((source, destination, expected))
    # Copy everything before removing any original; never overwrite a dataset file.
    for source, destination, expected in pairs:
        if destination.exists():
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + "." + uuid.uuid4().hex + ".tmp")
        try:
            shutil.copyfile(source, temporary)
            if file_hash(temporary) != expected:
                raise OSError(f"A verificacao da copia falhou: {source}")
            os.link(temporary, destination)
        finally:
            if temporary.exists():
                temporary.unlink()
    for source, destination, expected in pairs:
        if file_hash(destination) != expected:
            raise OSError(f"A verificacao do dataset falhou: {destination}")
        if source.exists() and file_hash(source) != expected:
            raise ValueError(f"A origem mudou durante a copia: {source}")
    receipts = dataset / "sessions"
    receipts.mkdir(parents=True, exist_ok=True)
    (receipts / (session_id + ".json")).write_text(serialized, encoding="utf-8")
    # Only delete the files listed and verified above, never recursively delete a folder.
    for source, destination, expected in pairs:
        if source.exists():
            source.unlink()
    video_kept = None
    if plan.get("video"):
        video = Path(plan["video"])
        if video.exists():
            if (not video.is_symlink() and video.is_file()
                    and not video.resolve().is_relative_to(dataset)
                    and file_hash(video) == plan.get("video_sha256")):
                video.unlink()
            else:
                video_kept = str(video)
    labels = folder / "labels"
    if labels.exists() and not any(labels.iterdir()):
        labels.rmdir()
    for metadata_name in ("session.json", "export_plan.json"):
        metadata_path = folder / metadata_name
        if metadata_path.exists():
            metadata_path.unlink()
    if not any(folder.iterdir()):
        folder.rmdir()
    return video_kept


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
        ttk.Button(root, text='Finalizar: enviar para treino e validacao',
                   command=self.finalize).pack(pady=5)
        self.load()

    def finalize(self):
        if self.dirty:
            messagebox.showinfo('Guardar primeiro', 'Guarda a anotacao atual antes de finalizar.')
            return
        finalize_session(self.root, self.folder)

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
        for offset in range(1, len(self.files) + 1):
            candidate = (self.index + offset) % len(self.files)
            label = self.labels / (self.files[candidate].stem + '.txt')
            if not label.exists():
                self.index = candidate
                self.load()
                return
        self.redraw()
        messagebox.showinfo('Sessao anotada', 'Todas as imagens estao anotadas. Carrega em Finalizar para enviar para o dataset.')

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


def finalize_session(root, folder):
    try:
        percent = 80
        if not (folder / "export_plan.json").exists():
            percent = simpledialog.askinteger("Divisao do dataset", "Percentagem para treino (o resto vai para validacao):",
                                              initialvalue=80, minvalue=1, maxvalue=99, parent=root)
            if percent is None:
                return
        plan = prepare_export(folder, percent)
        video_text = "Video original: nao identificado; nao sera apagado."
        if plan.get("video"):
            video_text = "Apagar o video original depois da copia: " + plan["video"]
        message = (f"Treino: {plan['train']} imagens\nValidacao: {plan['valid']} imagens\n"
                   f"Destino: {plan['dataset']}\n\n"
                   f"Apagar as imagens e labels desta sessao depois de verificar a copia:\n{folder}\n\n"
                   f"{video_text}\n\nConfirmar?")
        if not messagebox.askyesno("Finalizar sessao", message, parent=root):
            return
        root.config(cursor="watch")
        root.update_idletasks()
        kept = finish_export(plan)
        result = "Sessao adicionada ao dataset. Agora basta correr treinar.py."
        if kept:
            result += "\nO video mudou e foi preservado: " + kept
        if folder.exists():
            result += "\nOutros ficheiros na pasta foram preservados."
        messagebox.showinfo("Concluido", result, parent=root)
        root.destroy()
        return True
    except Exception as exc:
        root.config(cursor="")
        messagebox.showerror("Exportacao nao concluida", str(exc) + "\nPodes reabrir esta pasta para tentar novamente.", parent=root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', nargs='?', type=Path, help='Video ou pasta de imagens')
    args = parser.parse_args()
    root = tk.Tk()
    source = args.folder
    if source is None:
        selected = tk.StringVar(root)
        root.title('Anotar chamas')
        def choose_video():
            path = filedialog.askopenfilename(title='Escolhe o video', filetypes=[('Videos', '*.mp4 *.avi *.mkv *.mov *.h264'), ('Todos', '*.*')])
            if path:
                selected.set(path)
        def choose_folder():
            path = filedialog.askdirectory(title='Continuar uma pasta de imagens')
            if path:
                selected.set(path)
        ttk.Button(root, text='Abrir video novo', command=choose_video).pack(padx=30, pady=15)
        ttk.Button(root, text='Continuar imagens ja extraidas', command=choose_folder).pack(padx=30, pady=15)
        root.protocol('WM_DELETE_WINDOW', lambda: selected.set('__cancel__'))
        root.wait_variable(selected)
        if selected.get() == '__cancel__':
            root.destroy()
            return
        source = Path(selected.get())
        for widget in root.winfo_children():
            widget.destroy()
    try:
        source = source.resolve(strict=True)
        if source.is_file():
            every = simpledialog.askinteger('Extrair imagens', 'Guardar uma imagem a cada quantos fotogramas?', initialvalue=60, minvalue=1, parent=root)
            if every is None:
                root.destroy()
                return
            root.config(cursor='watch')
            root.update_idletasks()
            source = extract_frames(source, every)
            root.config(cursor='')
        if (source / 'export_plan.json').exists():
            if not finalize_session(root, source):
                root.destroy()
            return
        Annotator(root, source)
    except Exception as exc:
        messagebox.showerror('Nao foi possivel abrir', str(exc), parent=root)
        root.destroy()
        return
    root.mainloop()


if __name__ == '__main__':
    main()
