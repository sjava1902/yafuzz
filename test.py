import paramiko 

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

client = ssh_connect()
stdin, stdout, stderr = client.exec_command('pwd')
output = stdout.read().decode('utf-8')
print(output)