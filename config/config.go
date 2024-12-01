package config

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
)

type Config struct {
	Target string `json:"target"`
	Http string `json:"http"`
	Workdir string `json:"workdir"`
	KernelObj string `json:"kernel_obj"`
	Image string `image:"image"`
	Sshkey string `sshkey:"ssh_key"`
	Procs uint16 `sshkey:"procs"`
	VMtype string `VMType:"type"`
};

func (config Config) InitConfig(configPath string) error {
	fmt.Println("[INFO] Чтение конфигурационного файла...")
	data, _ := ioutil.ReadFile(configPath)
	err := json.Unmarshal(data, &config)
	if err != nil {
		return err
	}
	return err
}