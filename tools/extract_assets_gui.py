#!/usr/bin/env python3
"""A small window for tools/extract_assets.py: pick the game's zip, press Extract.

Uses tkinter, which comes with Python on Windows and macOS (Linux: the python3-tk package), so
nothing is installed beyond Pillow, which the extractor needs anyway. Start it with
`python3 extract_assets_gui.py [the game's zip]`, or by double-clicking "Extract Apeiron Assets.bat".

All the work is done by extract_assets.convert(); this file only asks for two paths, runs it on a
background thread so the window stays alive, and shows what it prints.
"""
import io
import os
import queue
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import extract_assets as ea


class _Pipe(io.TextIOBase):
    """stdout for the worker thread: hands each piece of text to the window."""
    def __init__(self, q):
        self.q = q

    def write(self, text):
        if text:
            self.q.put(('log', text))
        return len(text)

    def flush(self):
        pass


class App:
    def __init__(self, root, first_zip=None):
        self.root = root
        self.q = queue.Queue()
        self.worker = None
        self.result = None
        root.title('Apeiron asset extractor')
        root.minsize(620, 420)

        frm = ttk.Frame(root, padding=12)
        frm.pack(fill='both', expand=True)
        frm.columnconfigure(1, weight=1)
        frm.rowconfigure(5, weight=1)

        ttk.Label(frm, text='Turns your copy of Apeiron 1.0.3 into the artwork folder the SDL3 port needs.',
                  wraplength=580).grid(row=0, column=0, columnspan=3, sticky='w', pady=(0, 10))

        zip0 = first_zip or ea._find_game_zip() or ''
        self.src = tk.StringVar(value=zip0)
        self.out = tk.StringVar(value=ea.default_out_for(zip0) if zip0 else '')
        self.out_touched = False

        ttk.Label(frm, text="Game zip:").grid(row=1, column=0, sticky='w')
        e1 = ttk.Entry(frm, textvariable=self.src)
        e1.grid(row=1, column=1, sticky='ew', padx=6, pady=2)
        ttk.Button(frm, text='Browse...', command=self.pick_src).grid(row=1, column=2)
        self.src.trace_add('write', lambda *_: self.src_changed())

        ttk.Label(frm, text='Save the folder to:').grid(row=2, column=0, sticky='w')
        e2 = ttk.Entry(frm, textvariable=self.out)
        e2.grid(row=2, column=1, sticky='ew', padx=6, pady=2)
        ttk.Button(frm, text='Browse...', command=self.pick_out).grid(row=2, column=2)
        e2.bind('<Key>', lambda _e: setattr(self, 'out_touched', True))

        self.fonts = ttk.Label(frm, text=self.font_summary(), foreground='#555555', wraplength=580)
        self.fonts.grid(row=3, column=0, columnspan=3, sticky='w', pady=(6, 6))

        bar = ttk.Frame(frm)
        bar.grid(row=4, column=0, columnspan=3, sticky='ew')
        self.go = ttk.Button(bar, text='Extract', command=self.start)
        self.go.pack(side='left')
        self.show = ttk.Button(bar, text='Open the folder', command=self.open_result, state='disabled')
        self.show.pack(side='left', padx=8)
        self.prog = ttk.Progressbar(bar, mode='indeterminate', length=160)
        self.prog.pack(side='right')
        self.status = ttk.Label(bar, text='')
        self.status.pack(side='right', padx=8)

        box = ttk.Frame(frm)
        box.grid(row=5, column=0, columnspan=3, sticky='nsew', pady=(8, 0))
        self.log = tk.Text(box, height=12, wrap='word', state='disabled', font=('Consolas', 9))
        sb = ttk.Scrollbar(box, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side='right', fill='y')
        self.log.pack(side='left', fill='both', expand=True)

        root.after(100, self.poll)

    # ------------------------------------------------------------------ what the fonts will be
    @staticmethod
    def font_summary():
        have = []
        for name in ea.FONT_NAMES:
            real = any(os.path.isfile(os.path.join(d, name + '.zip')) or os.path.isfile(os.path.join(d, name + '.rsrc'))
                       for d in [HERE, os.path.join(HERE, '..')] + ea._home_dirs())
            have.append(name + (': Apple\'s, found' if real else ': free stand-in'))
        return 'Fonts - ' + ';  '.join(have) + '.  (Put Geneva.zip / Chicago.zip beside the game zip to use the originals.)'

    # ------------------------------------------------------------------ the two paths
    def pick_src(self):
        f = filedialog.askopenfilename(title="Apeiron 1.0.3 zip",
                                       filetypes=[('Zip files', '*.zip'), ('All files', '*.*')])
        if f:
            self.src.set(f)

    def pick_out(self):
        d = filedialog.askdirectory(title='Where should the apeiron_classic_assets folder go?')
        if d:
            self.out.set(os.path.join(d, ea.DEFAULT_OUT))
            self.out_touched = True

    def src_changed(self):
        # Until the output path has been touched by hand it follows the input: beside the zip.
        s = self.src.get().strip().strip('"')
        if not self.out_touched and s:
            self.out.set(ea.default_out_for(s))

    # ------------------------------------------------------------------ running it
    def say(self, text):
        self.log.configure(state='normal')
        self.log.insert('end', text)
        self.log.see('end')
        self.log.configure(state='disabled')

    def start(self):
        src = self.src.get().strip().strip('"')
        out = self.out.get().strip().strip('"')
        if not src or not os.path.exists(src):
            messagebox.showerror('Apeiron', "Choose the game's zip first.")
            return
        if not out:
            out = ea.default_out_for(src)
            self.out.set(out)
        self.result = None
        self.log.configure(state='normal')
        self.log.delete('1.0', 'end')
        self.log.configure(state='disabled')
        self.go.configure(state='disabled')
        self.show.configure(state='disabled')
        self.status.configure(text='Working...')
        self.prog.start(12)
        self.worker = threading.Thread(target=self.work, args=(src, out), daemon=True)
        self.worker.start()

    def work(self, src, out):
        old = sys.stdout
        sys.stdout = _Pipe(self.q)
        try:
            ea.convert(src, out)
            self.q.put(('done', out))
        except SystemExit as e:                     # the extractor's way of saying why it cannot go on
            self.q.put(('fail', str(e.code) if e.code not in (None, 0) else 'Stopped.'))
        except BaseException as e:                  # anything else: show it rather than die silently
            import traceback
            self.q.put(('fail', 'Something went wrong:\n\n' + traceback.format_exc()))
        finally:
            sys.stdout = old

    def poll(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == 'log':
                    self.say(val)
                elif kind == 'done':
                    self.finish(True, val)
                elif kind == 'fail':
                    self.say('\n' + val + '\n')
                    self.finish(False, val)
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def finish(self, ok, val):
        self.prog.stop()
        self.go.configure(state='normal')
        if ok:
            self.result = os.path.abspath(val)
            self.status.configure(text='Done')
            self.show.configure(state='normal')
            messagebox.showinfo('Apeiron',
                                'Done.\n\n%s\n\nPut that folder beside apeiron.exe on Windows, or in the '
                                'Download folder on Android / Google TV.' % self.result)
        else:
            self.status.configure(text='Stopped')
            messagebox.showerror('Apeiron', val[:1500])

    def open_result(self):
        if not self.result:
            return
        try:
            if os.name == 'nt':
                os.startfile(self.result)
            elif sys.platform == 'darwin':
                subprocess.Popen(['open', self.result])
            else:
                subprocess.Popen(['xdg-open', self.result])
        except OSError as e:
            messagebox.showerror('Apeiron', str(e))


def main():
    first = sys.argv[1].strip('"') if len(sys.argv) > 1 else None      # a zip dropped on the .bat
    root = tk.Tk()
    App(root, first)
    root.mainloop()


if __name__ == '__main__':
    main()
