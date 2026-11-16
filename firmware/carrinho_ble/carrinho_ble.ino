// Carrinho com L298N + ESP32-C3 SuperMini controlado por Bluetooth (BLE)
// Controle pela página web (Web Bluetooth): https://<seu-usuario>.github.io/carrinho-bluetooth/
//
// Protocolo: a página escreve 2 bytes na característica:
//   byte 0 = comando: 'F' frente, 'B' trás, 'L' esquerda, 'R' direita, 'S' parar
//   byte 1 = velocidade (0 a 255)
// Enquanto um botão está pressionado, a página repete o comando a cada 150 ms.
// Se nenhum comando chegar em 500 ms (ou o celular desconectar), o carro para sozinho.

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define NOME_BLE      "Carrinho"
#define SERVICE_UUID  "b6a1c0de-4f3a-4c8e-9d2a-7c1e5a0f1001"
#define CHAR_UUID     "b6a1c0de-4f3a-4c8e-9d2a-7c1e5a0f1002"

#define ENA 5   // Motor direito (OUT1/OUT2) - PWM
#define IN1 6
#define IN2 7
#define IN3 8   // Motor esquerdo (OUT3/OUT4)
#define IN4 9
#define ENB 10  // Motor esquerdo - PWM

#define TEMPO_SEGURANCA_MS 500  // para o carro se ficar sem comando
#define ARRANQUE_MS        120  // força total por um instante ao começar a andar

volatile char comandoRecebido = 'S';
volatile uint8_t velocidadeRecebida = 100;
volatile unsigned long ultimoComandoMs = 0;
volatile bool conectado = false;

char comandoAtual = 'S';
uint8_t velocidadeAtual = 0;
unsigned long fimArranqueMs = 0;
bool estavaConectado = false;

void motorDireito(int sentido) {   // 1 = frente, -1 = trás, 0 = parado
  digitalWrite(IN1, sentido == 1);
  digitalWrite(IN2, sentido == -1);
}

void motorEsquerdo(int sentido) {
  digitalWrite(IN3, sentido == -1);
  digitalWrite(IN4, sentido == 1);
}

void aplicarVelocidade(uint8_t v) {
  analogWrite(ENA, v);
  analogWrite(ENB, v);
}

void aplicar(char comando, uint8_t velocidade) {
  if (comando != comandoAtual) {
    switch (comando) {
      case 'F': motorDireito(1);  motorEsquerdo(1);  break;
      case 'B': motorDireito(-1); motorEsquerdo(-1); break;
      case 'L': motorDireito(1);  motorEsquerdo(-1); break;
      case 'R': motorDireito(-1); motorEsquerdo(1);  break;
      default:  comando = 'S'; motorDireito(0); motorEsquerdo(0); break;
    }

    if (comando == 'S') {
      aplicarVelocidade(0);
    } else {
      aplicarVelocidade(255);  // arranque para vencer a inércia
      fimArranqueMs = millis() + ARRANQUE_MS;
    }
    comandoAtual = comando;
  }

  velocidadeAtual = velocidade;
  if (comandoAtual != 'S' && millis() >= fimArranqueMs) {
    aplicarVelocidade(velocidadeAtual);
  }
}

class CallbacksServidor : public BLEServerCallbacks {
  void onConnect(BLEServer *servidor) override {
    conectado = true;
  }
  void onDisconnect(BLEServer *servidor) override {
    conectado = false;
    comandoRecebido = 'S';
  }
};

class CallbacksComando : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *caracteristica) override {
    size_t tamanho = caracteristica->getLength();
    uint8_t *dados = caracteristica->getData();
    if (tamanho >= 1) {
      comandoRecebido = (char)dados[0];
      if (tamanho >= 2) velocidadeRecebida = dados[1];
      ultimoComandoMs = millis();
    }
  }
};

void setup() {
  Serial.begin(115200);

  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  motorDireito(0); motorEsquerdo(0); aplicarVelocidade(0);

  BLEDevice::init(NOME_BLE);
  BLEServer *servidor = BLEDevice::createServer();
  servidor->setCallbacks(new CallbacksServidor());

  BLEService *servico = servidor->createService(SERVICE_UUID);
  BLECharacteristic *caracteristica = servico->createCharacteristic(
    CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  caracteristica->setCallbacks(new CallbacksComando());
  servico->start();

  BLEAdvertising *anuncio = BLEDevice::getAdvertising();
  anuncio->addServiceUUID(SERVICE_UUID);
  anuncio->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("Pronto! Procure por \"" NOME_BLE "\" na página de controle.");
}

void loop() {
  char comando = comandoRecebido;
  uint8_t velocidade = velocidadeRecebida;

  // Segurança: sem comando recente ou sem conexão, para.
  if (!conectado || millis() - ultimoComandoMs > TEMPO_SEGURANCA_MS) {
    comando = 'S';
  }

  aplicar(comando, velocidade);

  // Depois de desconectar, volta a anunciar para poder reconectar.
  if (estavaConectado && !conectado) {
    delay(300);
    BLEDevice::startAdvertising();
    Serial.println("Desconectado. Aguardando nova conexão...");
  }
  if (!estavaConectado && conectado) {
    Serial.println("Conectado!");
  }
  estavaConectado = conectado;

  delay(10);
}
