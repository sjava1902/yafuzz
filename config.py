import json
class Config:
    def __init__(self, file_path):
        with open(file_path, 'r') as file:
            config = json.load(file)
            self.target = config.get("target", "")
            self.http = config.get("http", "")
            self.workdir = config.get("workdir", "")
            self.kernel_obj = config.get("kernel_obj", "")
            # "http": "localhost:56741",
            # "workdir": "/home/lvc/syzkaller/workdir",
            # "kernel_obj": "/home/lvc/linux-stable",
            # "image": "/home/lvc/syzkaller/bullseye.img",
            # "sshkey": "/home/lvc/syzkaller/bullseye.id_rsa",
            # "syzkaller": "/home/lvc/syzkaller",
            # "procs": 4,
            # "type": "qemu",
