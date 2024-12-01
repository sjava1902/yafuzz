import paramiko
import subprocess
import time
import os
import json
#from flask import Flask, render_template_string
import threading
import json
import click
from config import Config

QEMU_CMD = f"""
qemu-system-x86_64 -m 6000 -smp 2 -chardev socket,id=SOCKSYZ,server=on,wait=off,host=localhost,port=43548 \
-mon chardev=SOCKSYZ,mode=control -display none -serial stdio -no-reboot -name VM-0 -device virtio-rng-pci \
-enable-kvm -cpu host,migratable=off -device e1000,netdev=net0 -netdev user,id=net0,restrict=on,hostfwd=tcp:127.0.0.1:2222-:22 \
-hda /home/slava/syzkaller/bullseye.img -snapshot -kernel /home/slava/linux/arch/x86/boot/bzImage \
-append "root=/dev/sda console=ttyS0" -nographic
"""

SSH_CMD = """
ssh -p 2222 \
    -F /dev/null \
    -o UserKnownHostsFile=/dev/null \
    -o IdentitiesOnly=yes \
    -o BatchMode=yes \
    -o StrictHostKeyChecking=no \
    -o ConnectTimeout=10 \
    -i /home/slava/Fuzz/bullseye.id_rsa \
    -v root@localhost \
    pwd
"""

SSH_PORT = 2222
SSH_KEY = "/home/slava/Fuzz/bullseye.id_rsa"
SSH_USER = "root"

REMOTE_DIR = "fuzz"
KCOV_DIR = "/sys/kernel/debug/kcov"
COVERAGE_OUTPUT = "/tmp/coverage"
COVERAGE_OUTPUT = "/tmp/coverage"
LOCAL_COVERAGE_FILE = "/home/slava/Fuzz/cov"

HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Coverage Analysis</title>
    <style>
        body {
            font-family: Arial, sans-serif;
        }
        .covered {
            background-color: #ffcccc;
        }
    </style>
</head>
<body>
    <h1>Code Coverage Analysis</h1>
    <ul>
        {% for file, lines in coverage.items() %}
        <li><strong>{{ file }}</strong></li>
        <pre>
        {% for line_num, code in lines %}
        <span class="covered">{{ line_num }}: {{ code }}</span>
        {% endfor %}
        </pre>
        {% endfor %}
    </ul>
</body>
</html>
"""
#app = Flask(__name__)


# Функция для запуска виртуальной машины
def start_vm():
    print("[INFO] Запуск виртуальной машины...")
    qemu_process = subprocess.Popen(QEMU_CMD, shell=True)
    time.sleep(30)  # Подождать, пока VM запустится
    return qemu_process

# Функция для выполнения команд на виртуальной машине
def run_command(client, command):
    print(f"[INFO] Выполнение команды: {command}")
    stdin, stdout, stderr = client.exec_command(command)
    stdout.channel.recv_exit_status()
    return stdout.read().decode(), stderr.read().decode()

# Функция для отправки файлов на виртуальную машину
# Функция для отправки файлов на виртуальную машину
def send_file(client, local_path, remote_path):
    print(f"[INFO] Копирование файла {local_path} на виртуальную машину...")
    sftp = client.open_sftp()
    try:
        # Убедиться, что директория существует
        dir_path = os.path.dirname(remote_path)
        try:
            sftp.stat(dir_path)
        except FileNotFoundError:
            print(f"[INFO] Директория {dir_path} не найдена. Создаю...")
            stdin, stdout, stderr = client.exec_command(f"mkdir -p {dir_path}")
            stdout.channel.recv_exit_status()

        # Отправка файла
        sftp.put(local_path, remote_path)
    finally:
        sftp.close()




# Настройка среды на виртуальной машине
def setup_vm(client):
    print("[INFO] Настройка виртуальной машины...")
    commands = [
        f"mkdir -p {REMOTE_DIR}",
        "modprobe kcov",
        f"chmod 777 {KCOV_DIR}",
        f"mkdir -p {COVERAGE_OUTPUT}"
    ]
    for cmd in commands:
        run_command(client, cmd)

# Функция для подключения по SSH
def ssh_connect():
    print("[INFO] Подключение к виртуальной машине по SSH...")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(
            "localhost",
            port=2222,
            username="root",
            key_filename="/home/slava/syzkaller/bullseye.id_rsa",
            look_for_keys=False,
            allow_agent=False,
            
        )
        print("[INFO] Подключение к виртуальной машине по SSH установлено")
    except Exception as e:
        print(f"[ERROR] Не удалось подключиться по SSH: {e}")
        exit(1)
    return client

# Запуск фаззинга файловой системы
def start_fuzzing(client):
    print("[INFO] Запуск фаззинга файловой системы...")
    fuzzing_command = f"""
    cd {REMOTE_DIR} && ./fuzzer --fs=gfs2 --kcov={KCOV_DIR} --output={COVERAGE_OUTPUT}
    """
    stdout, stderr = run_command(client, fuzzing_command)
    print("[INFO] Результаты фаззинга:")
    print(stdout)
    print(stderr)

# Скачивание покрытия
def download_coverage(ssh_client):
    print("[INFO] Скачивание покрытия...")
    LOCAL_COVERAGE_FILE = "output.txt"
    REMOTE_COVERAGE_PATH = "/root/output.txt"  # Исправленный путь

    try:
        # Проверяем, существует ли путь на виртуальной машине
        stdin, stdout, stderr = ssh_client.exec_command(f"ls {REMOTE_COVERAGE_PATH}")
        if stdout.channel.recv_exit_status() != 0:
            print(f"[ERROR] Файл покрытия не найден: {stderr.read().decode().strip()}")
            return

        # Скачиваем файл покрытия
        sftp = ssh_client.open_sftp()
        sftp.get(REMOTE_COVERAGE_PATH, LOCAL_COVERAGE_FILE)
        sftp.close()
        print(f"[INFO] Покрытие успешно скачано: {LOCAL_COVERAGE_FILE}")
    except Exception as e:
        print(f"[ERROR] Ошибка при скачивании покрытия: {e}")


# Разбор покрытия
def parse_coverage():
    print("[INFO] Разбор покрытия...")
    with open(LOCAL_COVERAGE_FILE, "r") as f:
        data = json.load(f)

    coverage = {}
    for entry in data.get("coverage", []):
        file, line_num = entry.split(":")
        if file not in coverage:
            coverage[file] = []
        coverage[file].append((line_num, f"Line {line_num} code snippet"))
    return coverage

# @app.route("/")
# def coverage():
#     coverage_data = parse_coverage()
#     return render_template_string(HTML_TEMPLATE, coverage=coverage_data)

# def start_web_server():
#     print("[INFO] Запуск веб-сервера...")
#     app.run(host="0.0.0.0", port=5000)


def cleanup():
    pass

if __name__ == "__main__":
    #Config('config.txt')
    print("[INFO] Подключение к виртуальной машине по SSH...")
    #qemu_process = start_vm()

    #  Подключение к qemu
    ssh_client = ssh_connect()
    run_command(ssh_client, "mount -t debugfs none /sys/kernel/debug")

    # Копирование фаззера и подготовка окружения
    # send_file(ssh_client, "fuzzer", f"{REMOTE_DIR}/fuzzer")
    # run_command(ssh_client, f"chmod +x {REMOTE_DIR}/fuzzer")
    # run_command(ssh_client, f"./{REMOTE_DIR}/fuzzer")

    send_file(ssh_client, "coverage", f"{REMOTE_DIR}/coverage")
    run_command(ssh_client, f"chmod +x {REMOTE_DIR}/coverage")
    run_command(ssh_client, f"./{REMOTE_DIR}/coverage")

    download_coverage(ssh_client)

    # # Запуск фаззинга и обработка покрытия
    # try:
    #     start_fuzzing(ssh_client)
    #     download_coverage(ssh_client)

    #     # # Запуск веб-сервера в отдельном потоке
    #     # web_thread = threading.Thread(target=start_web_server)
    #     # web_thread.start()
    #     # web_thread.join()

    # finally:
    #     ssh_client.close()
    #     #qemu_process.terminate()
    #     print("[INFO] Фаззинг завершен.")

    # ssh_client.close()
    print("[INFO] Фаззинг завершен.")


