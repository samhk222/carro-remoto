# Carrinho Bluetooth (ESP32-C3 + L298N)

Página web para controlar um carrinho de dois motores por Bluetooth (BLE), direto do navegador.

## Como usar

1. Grave o firmware [`firmware/carrinho_ble/carrinho_ble.ino`](firmware/carrinho_ble/carrinho_ble.ino) no ESP32-C3 SuperMini (Arduino IDE, placa **ESP32C3 Dev Module**).
2. Abra a página de controle no **Chrome** (Android, Windows, Mac ou Linux). No iPhone, use o app **Bluefy**.
3. Toque em **Conectar**, escolha **Carrinho** na lista e segure as setas para andar.

O carro para sozinho se você soltar o botão, sair da página ou perder a conexão.

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
