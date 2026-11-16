// Carrinho com L298N + ESP32-C3 SuperMini controlado por Bluetooth (BLE)
// com sensor ultrassônico HC-SR04 e modo automático.
//
// Controle pela página web (Web Bluetooth): https://samhk222.github.io/carro-remoto/
//
// Protocolo (a página escreve 2 bytes na característica de comando):
//   byte 0 = comando:
//     'F' frente, 'B' trás, 'L' esquerda, 'R' direita, 'S' parar  (modo manual)
//     'A' liga o modo automático, 'M' volta para o modo manual
//   byte 1 = velocidade (0 a 255)
// No modo manual, a página repete o comando a cada 150 ms enquanto o botão
// está pressionado. Sem comando por 500 ms (ou sem conexão), o carro para.
//
// O ESP envia a distância medida pela característica de status (notificação):
//   bytes 0-1 = distância em cm (uint16, little-endian; 0 = nada à vista)
//   byte 2    = modo (0 = manual, 1 = automático)
//
// Modo automático (regras pela distância até o obstáculo à frente):
//   menos de 5 cm   -> para
//   5 a 10 cm       -> vira para a direita
//   10 a 20 cm      -> vira para a esquerda
//   mais de 20 cm   -> anda para frente, acelerando aos poucos
//
// No modo manual, o carro também não anda para frente com obstáculo a menos
// de 5 cm (ré e curvas continuam liberadas para sair de perto).

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define NOME_BLE      "Carrinho"
#define SERVICE_UUID  "b6a1c0de-4f3a-4c8e-9d2a-7c1e5a0f1001"
#define CHAR_UUID     "b6a1c0de-4f3a-4c8e-9d2a-7c1e5a0f1002"  // comandos (escrita)
#define STATUS_UUID   "b6a1c0de-4f3a-4c8e-9d2a-7c1e5a0f1003"  // distância (notificação)

// Ponte H L298N
#define ENA 5   // Motor direito (OUT1/OUT2) - PWM
#define IN1 6
#define IN2 7
#define IN3 8   // Motor esquerdo (OUT3/OUT4)
#define IN4 9
#define ENB 10  // Motor esquerdo - PWM

// Sensor ultrassônico HC-SR04
// ATENÇÃO: o ECHO sai em 5V. Use um divisor de tensão (1kΩ + 2kΩ) antes do GPIO 1.
#define TRIG 0
#define ECHO 1

// Distâncias do modo automático (cm)
#define DIST_PARAR    5
#define DIST_DIREITA  10
#define DIST_ESQUERDA 20

// Ajuste se quiser ligar o carro já no modo automático, sem precisar do celular
#define MODO_AUTOMATICO_AO_LIGAR false

#define VELOCIDADE_INICIAL     100  // velocidade usada antes de a página mandar outra
#define VELOCIDADE_MIN_AUTO    80   // de onde a aceleração começa no modo automático
#define PASSO_ACELERACAO       4    // quanto acelera a cada leitura do sensor
#define TEMPO_SEGURANCA_MS     500  // modo manual: para se ficar sem comando
#define ARRANQUE_MS            120  // força total por um instante ao começar a andar
#define INTERVALO_SENSOR_MS    60   // o HC-SR04 precisa de ~60 ms entre leituras
#define INTERVALO_STATUS_MS    200  // de quanto em quanto tempo manda a distância

volatile char comandoRecebido = 'S';
volatile uint8_t velocidadeRecebida = VELOCIDADE_INICIAL;
volatile unsigned long ultimoComandoMs = 0;
volatile bool conectado = false;
volatile bool modoAutomatico = MODO_AUTOMATICO_AO_LIGAR;

BLECharacteristic *caracteristicaStatus = nullptr;

char comandoAtual = 'S';
unsigned long fimArranqueMs = 0;
bool estavaConectado = false;

int distanciaCm = 0;              // 0 = nada à vista (mais de ~4 m)
unsigned long ultimaLeituraMs = 0;
unsigned long ultimoStatusMs = 0;
int velocidadeAuto = VELOCIDADE_MIN_AUTO;

// ---------- Motores ----------

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

  if (comandoAtual != 'S' && millis() >= fimArranqueMs) {
    aplicarVelocidade(velocidade);
  }
}

// ---------- Sensor ----------

int lerDistanciaUmaVez() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  unsigned long duracao = pulseIn(ECHO, HIGH, 25000);  // até ~4 m
  if (duracao == 0) return 0;                           // nada à vista
  return duracao / 58;                                  // microssegundos -> cm
}

// Lê a distância a cada INTERVALO_SENSOR_MS e usa a mediana das 3 últimas
// leituras, para ignorar leituras falsas isoladas.
void atualizarDistancia() {
  static int ultimas[3] = {0, 0, 0};
  static int indice = 0;

  if (millis() - ultimaLeituraMs < INTERVALO_SENSOR_MS) return;
  ultimaLeituraMs = millis();

  ultimas[indice] = lerDistanciaUmaVez();
  indice = (indice + 1) % 3;

  int a = ultimas[0], b = ultimas[1], c = ultimas[2];
  distanciaCm = max(min(a, b), min(max(a, b), c));  // mediana
}

bool obstaculoMuitoPerto() {
  return distanciaCm > 0 && distanciaCm < DIST_PARAR;
}

// ---------- Modo automático ----------

void dirigirSozinho(uint8_t velocidadeMaxima) {
  if (distanciaCm > 0 && distanciaCm < DIST_PARAR) {
    velocidadeAuto = VELOCIDADE_MIN_AUTO;
    aplicar('S', 0);
  } else if (distanciaCm > 0 && distanciaCm < DIST_DIREITA) {
    velocidadeAuto = VELOCIDADE_MIN_AUTO;
    aplicar('R', velocidadeMaxima);
  } else if (distanciaCm > 0 && distanciaCm <= DIST_ESQUERDA) {
    velocidadeAuto = VELOCIDADE_MIN_AUTO;
    aplicar('L', velocidadeMaxima);
  } else {
    // Caminho livre: acelera aos poucos até a velocidade escolhida na página.
    if (comandoAtual != 'F') velocidadeAuto = VELOCIDADE_MIN_AUTO;
    aplicar('F', min(velocidadeAuto, (int)velocidadeMaxima));
  }
}

// ---------- Bluetooth ----------

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
    if (tamanho < 1) return;

    char comando = (char)dados[0];
    if (tamanho >= 2) velocidadeRecebida = dados[1];

    if (comando == 'A') {
      modoAutomatico = true;
    } else if (comando == 'M') {
      modoAutomatico = false;
      comandoRecebido = 'S';
    } else {
      comandoRecebido = comando;
    }
    ultimoComandoMs = millis();
  }
};

void enviarStatus() {
  if (!conectado || caracteristicaStatus == nullptr) return;
  if (millis() - ultimoStatusMs < INTERVALO_STATUS_MS) return;
  ultimoStatusMs = millis();

  uint8_t dados[3] = {
    (uint8_t)(distanciaCm & 0xFF),
    (uint8_t)((distanciaCm >> 8) & 0xFF),
    (uint8_t)(modoAutomatico ? 1 : 0)
  };
  caracteristicaStatus->setValue(dados, sizeof(dados));
  caracteristicaStatus->notify();
}

// ---------- Programa ----------

void setup() {
  Serial.begin(115200);

  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  motorDireito(0); motorEsquerdo(0); aplicarVelocidade(0);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  digitalWrite(TRIG, LOW);

  BLEDevice::init(NOME_BLE);
  BLEServer *servidor = BLEDevice::createServer();
  servidor->setCallbacks(new CallbacksServidor());

  BLEService *servico = servidor->createService(SERVICE_UUID);

  BLECharacteristic *caracteristicaComando = servico->createCharacteristic(
    CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  caracteristicaComando->setCallbacks(new CallbacksComando());

  caracteristicaStatus = servico->createCharacteristic(
    STATUS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  caracteristicaStatus->addDescriptor(new BLE2902());

  servico->start();

  BLEAdvertising *anuncio = BLEDevice::getAdvertising();
  anuncio->addServiceUUID(SERVICE_UUID);
  anuncio->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("Pronto! Procure por \"" NOME_BLE "\" na página de controle.");
}

void loop() {
  atualizarDistancia();

  if (modoAutomatico) {
    dirigirSozinho(velocidadeRecebida);

    // Acelera um pouco a cada leitura do sensor enquanto o caminho está livre.
    static unsigned long ultimaAceleracaoMs = 0;
    if (comandoAtual == 'F' && millis() - ultimaAceleracaoMs >= INTERVALO_SENSOR_MS) {
      ultimaAceleracaoMs = millis();
      velocidadeAuto = min(velocidadeAuto + PASSO_ACELERACAO, 255);
    }
  } else {
    char comando = comandoRecebido;

    // Segurança: sem comando recente ou sem conexão, para.
    if (!conectado || millis() - ultimoComandoMs > TEMPO_SEGURANCA_MS) {
      comando = 'S';
    }
    // Não deixa bater: para de ir para frente com obstáculo muito perto.
    if (comando == 'F' && obstaculoMuitoPerto()) {
      comando = 'S';
    }

    aplicar(comando, velocidadeRecebida);
  }

  enviarStatus();

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

  delay(5);
}
