# Opens a tool to draw boxes around flames in images.
# Saves the annotations as YOLO-format text files.
# After confirmation, copies images and labels into the dataset's train/valid folders.
# Verifies every copy before deleting the session frames and its unchanged source video.
# No manual file copying is needed; run train.py after finishing the session.
from pathlib import Path
import argparse
import os
import json
import math
import shutil
import uuid
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog

from extract_frames import extract_frames, file_hash


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
            raise ValueError('Each label line must contain class 0 and four coordinates.')
        x, y, w, h = map(float, fields[1:])
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (x, y, w, h)) or w <= 0 or h <= 0:
            raise ValueError('Invalid label coordinates.')
        boxes.append(((x-w/2)*width, (y-h/2)*height,
                      (x+w/2)*width, (y+h/2)*height))
    return boxes


DATASET_DIR = Path(__file__).resolve().parent.parent / "Datasets" / "fire_detection"


def excluded_images(folder):
    # Excluded frames stay recoverable but never enter training or validation.
    archive = folder / ".deleted"
    if archive.is_symlink():
        raise ValueError("The deleted-image folder must not be a link.")
    if not archive.exists():
        return []
    return [p for p in archive.iterdir()
            if p.is_file() and p.suffix.lower() in (".jpg", ".jpeg", ".png")]


def delete_session_image(folder, image, labels):
    folder = Path(folder).resolve(strict=True)
    image = Path(image)
    labels = Path(labels)
    if (folder / "export_plan.json").exists():
        raise ValueError("Finish the pending export before changing this session.")
    if (image.is_symlink() or image.resolve().parent != folder
            or labels.is_symlink() or labels.resolve() != folder / "labels"):
        raise ValueError("Delete image is only available for local session images.")
    if folder.is_relative_to(DATASET_DIR.resolve()):
        raise ValueError("Delete image is not available inside the finalized dataset.")
    if image.suffix.lower() not in (".jpg", ".jpeg", ".png"):
        raise ValueError("Unsupported image type.")
    label = labels / (image.stem + ".txt")
    if label.is_symlink():
        raise ValueError("The annotation must not be a link.")
    excluded_images(folder)
    archive = folder / ".deleted"
    archive.mkdir(exist_ok=True)
    target_image = archive / image.name
    target_label = archive / label.name
    if target_image.exists() or target_label.exists():
        raise ValueError("An excluded file already has this name.")
    # Move both files together; roll back if moving the annotation fails.
    image.rename(target_image)
    try:
        if label.exists():
            label.rename(target_label)
    except OSError:
        target_image.rename(image)
        raise


def prepare_export(folder, train_percent=80, dataset=None):
    # Persist the exact file list so an interrupted export can be resumed.
    from PIL import Image

    folder = Path(folder).resolve(strict=True)
    if dataset is None:
        dataset = DATASET_DIR
    dataset = Path(dataset).resolve()
    if folder.is_relative_to(dataset) or dataset.is_relative_to(folder):
        raise ValueError("Choose a capture folder outside the dataset.")
    plan_path = folder / "export_plan.json"
    if plan_path.exists():
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        if plan["folder"] != str(folder) or plan["dataset"] != str(dataset):
            raise ValueError("This session's destination has changed. Reopen it at its original location.")
        return plan
    if not 1 <= train_percent <= 99:
        raise ValueError("The training percentage must be between 1 and 99.")
    files = sorted(p for p in folder.iterdir()
                   if p.suffix.lower() in (".jpg", ".jpeg", ".png"))
    if len(files) < 2:
        raise ValueError("At least two images are required to split training and validation.")
    if len({p.stem.casefold() for p in files}) != len(files):
        raise ValueError("Some images have duplicate base names.")
    metadata = {}
    if (folder / "session.json").exists():
        metadata = json.loads((folder / "session.json").read_text(encoding="utf-8"))
        if not metadata.get("complete"):
            raise ValueError("Frame extraction for this session has not finished.")
        if "frames" in metadata and metadata["frames"] != len(files) + len(excluded_images(folder)):
            raise ValueError("The image count has changed since extraction. The video will be preserved.")
    session_id = uuid.uuid4().hex
    train_count = max(1, min(len(files) - 1, round(len(files) * train_percent / 100)))
    entries = []
    for index, image in enumerate(files):
        label = folder / "labels" / (image.stem + ".txt")
        if image.is_symlink() or label.is_symlink() or (folder / "labels").is_symlink():
            raise ValueError("The session must not contain links to other files.")
        if not label.is_file():
            raise ValueError(f"Missing annotation: {image.name}")
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
        raise ValueError("The session folder and dataset must be separate.")
    session_id = str(uuid.UUID(plan["id"]).hex)
    plan_path = folder / "export_plan.json"
    serialized = json.dumps(plan, indent=2)
    if plan_path.exists():
        if json.loads(plan_path.read_text(encoding="utf-8")) != plan:
            raise ValueError("The export plan has changed.")
    else:
        with plan_path.open("x", encoding="utf-8") as target:
            target.write(serialized)
    pairs = []
    for entry in plan["entries"]:
        if entry["split"] not in ("train", "valid"):
            raise ValueError("Invalid dataset split.")
        for kind, subfolder in [("image", "images"), ("label", "labels")]:
            name = entry[kind]
            if Path(name).name != name or name in (".", ".."):
                raise ValueError("Invalid filename.")
            source = folder / name
            if kind == "label":
                source = folder / "labels" / name
            if source.is_symlink() or source.parent.is_symlink():
                raise ValueError("The source must not be a link.")
            destination = dataset / entry["split"] / subfolder / (session_id + "_" + name)
            if not destination.resolve().is_relative_to(dataset):
                raise ValueError("Destination outside the dataset.")
            expected = entry[kind + "_hash"]
            if source.exists() and file_hash(source) != expected:
                raise ValueError(f"File changed since confirmation: {source}")
            if destination.exists():
                if destination.is_symlink() or file_hash(destination) != expected:
                    raise ValueError(f"Existing destination has different contents: {destination}")
            elif not source.is_file():
                raise ValueError(f"Missing file: {source}")
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
                raise OSError(f"Copy verification failed: {source}")
            os.link(temporary, destination)
        finally:
            if temporary.exists():
                temporary.unlink()
    for source, destination, expected in pairs:
        if file_hash(destination) != expected:
            raise OSError(f"Dataset verification failed: {destination}")
        if source.exists() and file_hash(source) != expected:
            raise ValueError(f"Source changed during copying: {source}")
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
            raise ValueError('No images found directly in this folder.')
        if len({p.stem.casefold() for p in self.files}) != len(self.files):
            raise ValueError('Some images have the same base name. Rename them first.')
        if folder.name == 'images' and folder.parent.name in ('train', 'valid'):
            self.labels = folder.parent / 'labels'
        else:
            self.labels = folder / 'labels'
        self.labels.mkdir(exist_ok=True)
        self.index = next((i for i,p in enumerate(self.files)
                           if not (self.labels / (p.stem+'.txt')).exists()), 0)
        self.boxes, self.dirty, self.start = [], False, None
        root.title('Annotate flames')
        root.geometry('1100x850')
        root.minsize(850, 650)
        self.heading = ttk.Label(root, font=('Segoe UI', 12, 'bold'))
        self.heading.pack(pady=10)
        ttk.Label(root, text='Drag around EACH flame. You can draw multiple boxes.').pack()
        self.canvas = tk.Canvas(root, background='#20242a', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, padx=12, pady=10)
        self.status = ttk.Label(root)
        self.status.pack(pady=5)
        buttons = ttk.Frame(root)
        buttons.pack(pady=10)
        for text, action in [('Previous', lambda:self.navigate(-1)),
                             ('Undo box (Ctrl+Z)', self.undo),
                             ('Clear boxes', self.clear),
                             ('Save and next (Enter)', self.save),
                             ('No flame (N)', self.no_flame),
                             ('Skip', lambda:self.navigate(1))]:
            ttk.Button(buttons, text=text, command=action).pack(side='left', padx=3)
        ttk.Button(root, text='Delete image', command=self.delete_image).pack(pady=(0, 5))
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
        ttk.Button(root, text='Finalize: send to training and validation',
                   command=self.finalize).pack(pady=5)
        self.load()

    def finalize(self):
        if self.dirty:
            messagebox.showinfo('Save first', 'Save the current annotation before finalizing.')
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
        state='Unsaved changes' if self.dirty else ('Annotated' if self.label_path().exists() else 'Not annotated')
        self.heading.config(text=f'{self.index+1}/{len(self.files)} — {self.files[self.index].name}')
        self.status.config(text=f'{state} | {len(self.boxes)} boxes | {saved}/{len(self.files)} annotated images')

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
        if self.boxes and messagebox.askyesno('Clear','Remove all boxes from this image?'):
            self.boxes=[]
            self.dirty=True
            self.redraw()

    def delete_image(self):
        image = self.files[self.index]
        if not messagebox.askyesno(
                'Delete image',
                f'Remove {image.name} and its annotation from this session?\n'
                'Unsaved boxes will be discarded. The files remain recoverable in .deleted '
                'and will not enter the dataset.', parent=self.root):
            return
        try:
            delete_session_image(self.folder, image, self.labels)
        except (OSError, ValueError) as exc:
            messagebox.showerror('Could not delete image', str(exc), parent=self.root)
            return
        self.files.pop(self.index)
        self.dirty, self.start = False, None
        if not self.files:
            messagebox.showinfo('Empty session', 'No images remain to annotate.', parent=self.root)
            self.root.destroy()
            return
        self.index = min(self.index, len(self.files) - 1)
        self.load()

    def save(self):
        if not self.boxes:
            messagebox.showinfo('No boxes','If there is no flame, use "No flame". To leave the image unannotated, use "Skip".')
            return
        self.write_label()

    def no_flame(self):
        if self.boxes and not messagebox.askyesno('No flame','Delete the boxes and mark this image as having NO flame?'):
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
            messagebox.showerror('Could not save',str(exc))
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
        messagebox.showinfo('Session annotated', 'All images are annotated. Click Finalize to send them to the dataset.')

    def can_leave(self):
        return not self.dirty or messagebox.askyesno('Unsaved changes','Discard changes to this image?')

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
            percent = simpledialog.askinteger("Dataset split", "Training percentage (the remainder goes to validation):",
                                              initialvalue=80, minvalue=1, maxvalue=99, parent=root)
            if percent is None:
                return
        plan = prepare_export(folder, percent)
        video_text = "Original video: unidentified; it will not be deleted."
        if plan.get("video"):
            video_text = "Delete the original video after copying: " + plan["video"]
        message = (f"Training: {plan['train']} images\nValidation: {plan['valid']} images\n"
                   f"Destination: {plan['dataset']}\n\n"
                   f"Delete this session's images and labels after verifying the copy:\n{folder}\n\n"
                   f"{video_text}\n\nConfirm?")
        if not messagebox.askyesno("Finalize session", message, parent=root):
            return
        root.config(cursor="watch")
        root.update_idletasks()
        kept = finish_export(plan)
        result = "Session added to the dataset. You can now run train.py."
        if kept:
            result += "\nThe video changed and was preserved: " + kept
        if folder.exists():
            result += "\nOther files in the folder were preserved."
        messagebox.showinfo("Completed", result, parent=root)
        root.destroy()
        return True
    except Exception as exc:
        root.config(cursor="")
        messagebox.showerror("Export incomplete", str(exc) + "\nYou can reopen this folder to try again.", parent=root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', nargs='?', type=Path, help='Video or image folder')
    args = parser.parse_args()
    root = tk.Tk()
    source = args.folder
    if source is None:
        selected = tk.StringVar(root)
        root.title('Annotate flames')
        def choose_video():
            path = filedialog.askopenfilename(title='Choose a video', filetypes=[('Videos', '*.mp4 *.avi *.mkv *.mov *.h264'), ('All files', '*.*')])
            if path:
                selected.set(path)
        def choose_folder():
            path = filedialog.askdirectory(title='Resume an image folder')
            if path:
                selected.set(path)
        ttk.Button(root, text='Open new video', command=choose_video).pack(padx=30, pady=15)
        ttk.Button(root, text='Resume extracted images', command=choose_folder).pack(padx=30, pady=15)
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
            every = simpledialog.askinteger('Extract images', 'Save one image every how many frames?', initialvalue=60, minvalue=1, parent=root)
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
        messagebox.showerror('Could not open', str(exc), parent=root)
        root.destroy()
        return
    root.mainloop()


if __name__ == '__main__':
    main()
