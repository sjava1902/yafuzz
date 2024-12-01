package main

import (
	"fmt"
	"io"
	"log"
	"os"

	"golang.org/x/crypto/ssh"
	"github.com/pkg/sftp"

	"github.com/sjava1902/yafuzz/config"
)

// Конфигурация
const (
	qemuCmd         = "qemu-system-x86_64 -m 6000 -smp 2 -chardev socket,id=SOCKSYZ,server=on,wait=off,host=localhost,port=43548 -mon chardev=SOCKSYZ,mode=control -display none -serial stdio -no-reboot -name VM-0 -device virtio-rng-pci -enable-kvm -cpu host,migratable=off -device e1000,netdev=net0 -netdev user,id=net0,restrict=on,hostfwd=tcp:127.0.0.1:2222-:22 -hda /path/to/image/bullseye.img -snapshot -kernel /home/slava/linux/arch/x86/boot/bzImage -append \"root=/dev/sda console=ttyS0\" -nographic"
	sshAddress      = "localhost:2222"
	sshPrivateKey   = "/home/slava/Fuzz/bullseye.id_rsa"
	remoteCoverage  = "/root/output.txt"
	localCoverage   = "output.txt"
	remoteDir       = "/root/yafuzz"
)

func main() {
	// fmt.Println("[INFO] Запуск QEMU...")
	// cmd := exec.Command("sh", "-c", qemuCmd)
	// err := cmd.Start()
	// if err != nil {
	// 	log.Fatalf("[ERROR] Не удалось запустить QEMU: %v", err)
	// }
	// defer cmd.Process.Kill()

	// time.Sleep(30 * time.Second) // Ждем, пока VM запустится
	var config config.Config
	config.InitConfig("config.txt")
	
	client, err := sshConnect(sshAddress, sshPrivateKey)
	if err != nil {
		log.Fatalf("[ERROR] Ошибка подключения по SSH: %v", err)
	}
	defer client.Close()

	fmt.Println("[INFO] Настройка среды на виртуальной машине...")
	setupVM(client)

	err = sendFile(client, "fuzzer", remoteDir, true)
	if err != nil {
		log.Fatalf("[ERROR] Ошибка копирования файла по SSH: %v", err)
	}

	fmt.Println("[INFO] Запуск фаззинга...")
	err = runCommand(client, fmt.Sprintf("cd %s && ./fuzzer --fs=gfs2 --kcov=/sys/kernel/debug/kcov --output=%s", remoteDir, remoteCoverage))
	if err != nil {
		log.Fatalf("[ERROR] Ошибка выполнения фаззинга: %v", err)
	}

	fmt.Println("[INFO] Загрузка покрытия на хост...")
	err = downloadCoverage(client, remoteCoverage, localCoverage)
	if err != nil {
		log.Fatalf("[ERROR] Ошибка загрузки покрытия: %v", err)
	}
}

// sshConnect подключается к удаленной машине по SSH
func sshConnect(addr, privateKeyPath string) (*ssh.Client, error) {
	fmt.Println("[INFO] Подключение к виртуальной машине по SSH...")

	// Загрузка приватного ключа
	key, err := os.ReadFile(privateKeyPath)
	if err != nil {
		return nil, fmt.Errorf("не удалось загрузить приватный ключ: %w", err)
	}

	// Создание ssh.Signer из приватного ключа
	signer, err := ssh.ParsePrivateKey(key)
	if err != nil {
		return nil, fmt.Errorf("не удалось обработать приватный ключ: %w", err)
	}

	// Конфигурация клиента
	config := &ssh.ClientConfig{
		User: "root",
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(signer),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(), // Использовать для тестов; избегайте в продакшене
	}

	// Подключение к виртуальной машине
	address := fmt.Sprintf("%s:%s", "localhost", "2222")
	client, err := ssh.Dial("tcp", address, config)
	if err != nil {
		return nil, fmt.Errorf("ошибка подключения по SSH: %w", err)
	}

	return client, nil
}

// setupVM выполняет настройку среды на виртуальной машине
func setupVM(client *ssh.Client) {
	commands := []string{
		fmt.Sprintf("mkdir -p %s", remoteDir),
		"mount -t debugfs none /sys/kernel/debug",
	}
	for _, cmd := range commands {
		if err := runCommand(client, cmd); err != nil {
			log.Printf("[ERROR] Ошибка выполнения команды: %s: %v", cmd, err)
		}
	}
}

// runCommand выполняет команду на удаленной машине
func runCommand(client *ssh.Client, cmd string) error {
	session, err := client.NewSession()
	if err != nil {
		return fmt.Errorf("не удалось создать сессию SSH: %w", err)
	}
	defer session.Close()

	output, err := session.CombinedOutput(cmd)
	if err != nil {
		return fmt.Errorf("ошибка выполнения команды: %s: %w", output, err)
	}
	return nil
}

// downloadCoverage загружает файл покрытия с виртуальной машины
func downloadCoverage(client *ssh.Client, remotePath, localPath string) error {
	// Устанавливаем новую сессию SSH
	session, err := client.NewSession()
	if err != nil {
		return fmt.Errorf("не удалось создать SSH-сеанс: %w", err)
	}
	defer session.Close()

	// Открываем локальный файл для записи
	localFile, err := os.Create(localPath)
	if err != nil {
		return fmt.Errorf("не удалось создать локальный файл: %w", err)
	}
	defer localFile.Close()

	// Запускаем команду SCP для передачи файла
	go func() {
		w, _ := session.StdinPipe()
		defer w.Close()
		fmt.Fprintf(w, "scp -f %s\n", remotePath)
	}()

	// Читаем содержимое удаленного файла
	stdout, err := session.StdoutPipe()
	if err != nil {
		return fmt.Errorf("не удалось получить stdout: %w", err)
	}

	_, err = io.Copy(localFile, stdout)
	if err != nil {
		return fmt.Errorf("ошибка копирования данных: %w", err)
	}

	// Запускаем SCP-команду
	if err := session.Run(fmt.Sprintf("scp -f %s", remotePath)); err != nil {
		return fmt.Errorf("ошибка выполнения SCP: %w", err)
	}

	fmt.Printf("[INFO] Файл %s успешно загружен в %s\n", remotePath, localPath)
	return nil
}

// Функция для отправки файла через SSH
func sendFile(client *ssh.Client, localPath, remotePath string, isExecutable bool) error {
	fmt.Printf("[INFO] Копирование файла %s на виртуальную машину...\n", localPath)
	// Открываем локальный файл
	sftp, err := sftp.NewClient(client)
    if err != nil {
        return err
    }
    defer sftp.Close()
    // Open the source file
    srcFile, err := os.Open(localPath)
    if err != nil {
        return err
    }
    defer srcFile.Close()
    // Create the destination file
    dstFile, err := sftp.Create(remotePath + "/" + localPath)
    if err != nil {
        return err
    }
    defer dstFile.Close()
    // write to file
    if  _, err := dstFile.ReadFrom(srcFile); err!= nil {
        return err
    }

	if (isExecutable) {
		commands := []string{
			fmt.Sprintf("chmod +x %s", remoteDir + "/" + localPath),
		}
		for _, cmd := range commands {
			if err := runCommand(client, cmd); err != nil {
				log.Printf("[ERROR] Ошибка выполнения команды: %s: %v", cmd, err)
			}
		}
	}
    return nil
}
