# Carrinho Bluetooth (ESP32-C3 + L298N)

Página web para controlar um carrinho de dois motores por Bluetooth (BLE), direto do navegador.

## Como usar

1. Grave o firmware [`firmware/carrinho_ble/carrinho_ble.ino`](firmware/carrinho_ble/carrinho_ble.ino) no ESP32-C3 SuperMini (Arduino IDE, placa **ESP32C3 Dev Module**).
2. Abra a página de controle no **Chrome** (Android, Windows, Mac ou Linux). No iPhone, use o app **Bluefy**.
3. Toque em **Conectar**, escolha **Carrinho** na lista e segure as setas para andar.

O carro para sozinho se você soltar o botão, sair da página ou perder a conexão.

## Modo automático (sensor HC-SR04)

Toque em **Modo automático** na página e o carro dirige sozinho pela distância até o obstáculo à frente:

| Distância | O carro |
|---|---|
| menos de 5 cm | para |
| 5 a 10 cm | vira para a direita |
| 10 a 20 cm | vira para a esquerda |
| mais de 20 cm | anda para frente, acelerando aos poucos até a velocidade escolhida |

Toque em **PARAR** para voltar ao controle manual. No modo manual, o carro também não anda para frente com obstáculo a menos de 5 cm.

## Ligações

| L298N | ESP32-C3 |
|---|---|
| ENA | GPIO 5 |
| IN1 | GPIO 6 |
| IN2 | GPIO 7 |
| IN3 | GPIO 8 |
| IN4 | GPIO 9 |
| ENB | GPIO 10 |
| GND | GND |

OUT1/OUT2 = motor direito, OUT3/OUT4 = motor esquerdo.

| HC-SR04 | ESP32-C3 |
|---|---|
| VCC | 5V |
| TRIG | GPIO 0 |
| ECHO | GPIO 1 **pelo divisor de tensão** (1 kΩ do ECHO ao GPIO 1, 2 kΩ do GPIO 1 ao GND) |
| GND | GND |

O ECHO do HC-SR04 sai em 5V e o ESP32-C3 aceita no máximo 3,3V: sem o divisor, o pino pode queimar.
# carro-remoto
