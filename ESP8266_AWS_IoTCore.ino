#include "FS.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Thread.h>

#define       LedBoard   2                             // WIFI Module LED
#define       BUTTON     D3                            // NodeMCU Button

//Dados da rede wifi
const char* ssid = "AIRLIVE2";
const char* password = "ead00177943";

const long utcOffsetInSeconds = -10800;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
Thread threadPublish;
Thread threadSetKeep;
Thread threadSerial;
Thread threadGravarHistorico;

/* Tópicos */
//============================================================================
const char* AWS_endpoint = "a1u5p9d1s2ikgy-ats.iot.us-east-1.amazonaws.com"; //MQTT broker ip
const char * topic_setKeepAlive = "esp8266/Timer";
const char * topic_modoOp = "esp8266/mode";
const char * topic_res_timer = "esp8266/Timer/res";
const char * topic_res_modoOp = "esp8266/modeOp/res";


/*variáveis globais*/
//============================================================================
#define BUFFER_LEN 256
int gx, gy, gz, ax, ay, az, i;
float Gx, Gy, Gz, Ax, Ay, Az;
long lastMsg = 0;
char msg[BUFFER_LEN];
int value = 0;
uint16_t valKeepAlive = 30;
uint16_t keepAliveAux = 30;
int modoOp = 0;
int status = 0; //VARIÁVEL QUE CONTROLA O STATUS (LIGADO / DESLIGADO)
int time_min = 0;
int time_sec = 0;
int LedDelay = 0;
String arquivo = "/historico.txt";
struct Date {
  int dayOfWeek;
  int day;
  int month;
  int year;
  int hours;
  int minutes;
  int seconds;
};
//============================================================================

/*-------------------------------------------------------------------------------------------
  Função que recebe a mensagem no tópico
  -------------------------------------------------------------------------------------------*/
void callback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, topic_setKeepAlive) == 0) {
    DynamicJsonDocument doc4(1024);
    deserializeJson(doc4, payload);
    valKeepAlive = doc4["connectionTime"];
    doc4["id"] = 1;
    char timer[128];
    serializeJson(doc4, timer);
    valKeepAlive = valKeepAlive * 60;
    valKeepAlive /= 1.5;
    Serial.printf("keep = %f \n", valKeepAlive);
    respMqtt("esp8266/Timer/res", timer);
  }
  if (strcmp(topic, topic_modoOp) == 0) {
    DynamicJsonDocument doc4(1024);
    deserializeJson(doc4, payload);
    modoOp = doc4["modeOp"];
    doc4["id"] = 1;
    char modeOperacao[128];
    serializeJson(doc4, modeOperacao);
    Serial.printf("modoOp:%d", modoOp);
    if (modoOp == 1) {
      Serial.println("modo alarme");
    } else {
      Serial.println("modo detecção de acidente");
    }
    respMqtt("esp8266/modeOp/res", modeOperacao);
  }
}

WiFiClientSecure espClient;
PubSubClient client(AWS_endpoint, 8883, callback, espClient); //define a porta mqtt

/*-------------------------------------------------------------------------------------------
  Função para confirmar recebimento em tópico
  -------------------------------------------------------------------------------------------*/
void respMqtt(char* topic, char* payload) {
  client.publish(topic, payload);
}

/*-------------------------------------------------------------------------------------------
  Função para configurar o acesso a rede wifi
  -------------------------------------------------------------------------------------------*/
void setup_wifi() {
  delay(10);
  //conexão com o wifi
  espClient.setBufferSizes(512, 512);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  espClient.setX509Time(timeClient.getEpochTime());

}

/*-------------------------------------------------------------------------------------------
  Função para reconectar caso ocorra interrupção na conexão
  -------------------------------------------------------------------------------------------*/
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    //configuração do willTopic
    byte willQoS = 0;
    const char* willTopic = "esp8266/status";
    boolean willRetain = false;
    DynamicJsonDocument doc(1024);
    doc["id"] = 1;
    doc["status_connection"] = 0;
    char willMessage[128];
    serializeJson(doc, willMessage);
    // Attempt to connect
    if (client.connect("1234", willTopic, willQoS, willRetain, willMessage)) {
      Serial.println("connected");
      // publica quando conectado

      client.publish("esp8266/status", "conectado");
      //Inscrição nos tópicos
      client.subscribe(topic_setKeepAlive);
      client.subscribe(topic_modoOp);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      char buf[256];
      espClient.getLastSSLError(buf, 256);
      Serial.print("WiFiClientSecure SSL error: ");
      Serial.println(buf);

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/*-------------------------------------------------------------------------------------------
  Função para retornar a primeira data do log presente no arquivo .txt
  -------------------------------------------------------------------------------------------*/
Date retornaDataHistorico() {
  File myFile = SPIFFS.open(arquivo, "r");
  if (!myFile) {
    Serial.println("Problema ao tentar ler, sorry...");
    exit(0);
  }
  int fileSize = myFile.size();
  Date date;
  if (fileSize > 0) {
    String dados = myFile.readStringUntil('-');
    char* data = (char*)dados.c_str();
    sscanf(data, "%d:%d %d/%d", &date.hours, &date.minutes, &date.day, &date.month);
  } else {
    Serial.println("arquivo vazio");
  }
  myFile.close();
  return date;
}

/*-------------------------------------------------------------------------------------------
  Função para retornar data atual
  -------------------------------------------------------------------------------------------*/
Date retornaDataAtual() {
  String formattedDate = timeClient.getFormattedDate();
  Date date;
  char* strDate = (char*)formattedDate.c_str();
  sscanf(strDate, "%d-%d-%dT%d:%d:%dZ",
         &date.year,
         &date.month,
         &date.day,
         &date.hours,
         &date.minutes,
         &date.seconds);
  return date;
}

/*-------------------------------------------------------------------------------------------
  Função para limpar arquivo .txt
  -------------------------------------------------------------------------------------------*/
void limparArquivo() {
  bool ready = SPIFFS.begin();
  if (!ready) {
    Serial.println("Suporte ao fs falhou!");
    exit(0);
  }
  File historicoFile = SPIFFS.open(arquivo, "w");
  if (historicoFile) {
    historicoFile.close();
    Serial.println("arquivo limpo");
  } else {
    Serial.println("falha ao limpar arquivo");
  }
  SPIFFS.end();
}

/*-------------------------------------------------------------------------------------------
  Função para retornar log valido
  -------------------------------------------------------------------------------------------*/
String removerLogInvalido(int horaAtual) {
  File myFile = SPIFFS.open(arquivo, "r");
  if (!myFile) {
    Serial.println("Problema ao tentar ler, sorry...");
    exit(0);
  }
  String dadosValidos = "";
  int fileSize = myFile.size();
  while (myFile.available()) {
    String horarioHistorico = myFile.readStringUntil('\n');
    int split = horarioHistorico.indexOf(':');
    String horaString = horarioHistorico.substring(0, split);
    char* horaChar = (char*) horaString.c_str();
    int primeiraHoraLog = atoi(horaChar);
    if (primeiraHoraLog > horaAtual) {
      dadosValidos = dadosValidos + horarioHistorico + "\n";
    } else {
      Serial.printf("removerLogInvalido log-->%d\n", primeiraHoraLog);
    }
  }
  myFile.close();
  return dadosValidos;
}

/*-------------------------------------------------------------------------------------------
  Função para gravar log no arquivo .txt
  -------------------------------------------------------------------------------------------*/
void gravarLog(String dado) {
  bool ready = SPIFFS.begin();
  if (!ready) {
    Serial.println("Suporte ao fs falhou!");
    exit(0);
  }
  bool createFile = SPIFFS.exists(arquivo);
  if (createFile) {
    Serial.println("Registrando Log...");
    File myFile = SPIFFS.open(arquivo, "a+");
    if (!myFile) {
      Serial.println("Problema ao tentar ler, sorry...");
      exit(0);
    }
    int fileSize = myFile.size();
    //retorna o horário atual
    Date dataAtual = retornaDataAtual();
    if (fileSize > 0) {
      Date dataHistorico = retornaDataHistorico();
      if ((dataAtual.day - dataHistorico.day) <= 1) {
        if (dataAtual.hours <= dataHistorico.hours ) {
          myFile.printf("%d:%d %d/%d --- %s\r\n", dataAtual.hours, dataAtual.minutes, dataAtual.day, dataAtual.month, dado.c_str());
          myFile.close();
        } else {
          myFile.close();
          String dadosValidosStr = removerLogInvalido(dataAtual.hours);
          char* dadosValidadosChar = (char*)dadosValidosStr.c_str();
          myFile = SPIFFS.open(arquivo, "w");
          if (!myFile) {
            Serial.println("Problema ao tentar ler, sorry...");
            exit(0);
          }
          if (strlen(dadosValidadosChar) > 1) {
            myFile.printf("%s", dadosValidadosChar);
          }
          myFile.printf("%d:%d %d/%d --- %s\r\n", dataAtual.hours, dataAtual.minutes, dataAtual.day, dataAtual.month, dado.c_str());
          myFile.close();
        }
      } else {
        limparArquivo();
      }
    } else {
      myFile.printf("%d:%d %d/%d --- %s\r\n", dataAtual.hours, dataAtual.minutes, dataAtual.day, dataAtual.month, dado.c_str());
      myFile.close();
    }
    Serial.println("Log registrado!");
  } else {
    Serial.println("Arquivo inexistente");
  }
  SPIFFS.end();
}

/*-------------------------------------------------------------------------------------------
  Função para ler arquivo .txt
  -------------------------------------------------------------------------------------------*/
void lerArquivo(String arquivo) {
  bool ready = SPIFFS.begin();
  if (!ready) {
    Serial.println("Suporte ao fs falhou!");
    exit(0);
  }
  bool createFile = SPIFFS.exists(arquivo);
  if (createFile) {
    Serial.println("Lendo arquivo...");
    File myFile = SPIFFS.open(arquivo, "r");
    if (!myFile) {
      Serial.println("Problema ao tentar ler, sorry...");
      exit(0);
    }
    int fileSize = myFile.size();
    //String fileName = myFile.name();
    Serial.printf("Size=%d\r\n", fileSize);
    while (myFile.available()) {
      String data = myFile.readStringUntil('\n');
      Serial.println(data);
    }
    if (fileSize == 0) {
      Serial.println("arquivo vazio");
    }
    myFile.close();
  } else {
    Serial.println("Arquivo inexistente");
  }
  SPIFFS.end();
}

/*-------------------------------------------------------------------------------------------
  Função para ler dados do console
  -------------------------------------------------------------------------------------------*/
void leituraDeDados() {
  if (Serial.available() > 0) {
    if (i == 0) {
      gx = Serial.read();
      gx -= 127;
      Gx = gx * 1.96;
    } else if (i == 1) {
      gy = Serial.read();
      gy -= 127;
      Gy = gy * 1.96;
    } else if (i == 2) {
      gz = Serial.read();
      gz -= 127;
      Gz = gz * 1.96;
    } else if (i == 3) {
      ax = Serial.read();
      ax -= 127;
      Ax = ax * 0.015;
    } else if (i == 4) {
      ay = Serial.read();
      ay -= 127;
      Ay = ay * 0.015;
    } else if (i == 5) {
      az = Serial.read();
      az -= 127;
      Az = az * 0.015;
      i = -1;
    }
    i++;
  }

  if (gx == 128) {
    lerArquivo(arquivo);
    gx = 0;
    Gx = 0;
    return;
  }

  if (modoOp == 0) {
    if (Gx  >= 88.2) {
      String msg = "Tombou para esquerda";
      Serial.println("Acidente detectado");
      acionarLed(msg);
    } else if (Gx  <= -88.2) {
      String msg = "Tombou para direita";
      Serial.println("Acidente detectado");
      acionarLed(msg);
    } else if (Gy  >= 147) {
      String msg = "Tombou para tras";
      Serial.println("Acidente detectado");
      acionarLed(msg);
    } else if (Gy  <= -98) {
      String msg = "Tombou para frente";
      Serial.println("Acidente detectado");
      acionarLed(msg);
    } else if (Gz  <= -176.4 || Gz  >= 176.4) {
      String msg = "Capotado";
      Serial.println("Acidente detectado");
      acionarLed(msg);
    } else if (Ax <= -1.8 || Ax >= 1.8 || Ay <= -1.8 || Ay >= 1.8 || Az <= -1.8 || Az >= 1.8) {
      String msg = "Batida";
      Serial.println("Acidente detectado");
      acionarLed(msg);
    } else {
      Serial.println("Dirigindo normalmente");
    }




  } else if (modoOp == 1) { //algum valor corresponde a 20° do giroscópio ou 3m/s no acelerometro.
    if (Gx >= 39.2 || Gx <= -39.2 || Gy >= 39.2 || Gy <= -39.2 || Ax >= 0.306 || Ax <= -0.306 || Ay >= 0.306 || Ay <= -0.306) {
      String msg = "Furto";
      Serial.println("Furto detectado");
      acionarLed(msg);
    }
  }

}

void acionarLed(String msg) {
  Serial.println("Ativando sistema de segurança, ligando para o número definido");
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();
  int lead2 = digitalRead(LedBoard);
  if (lead2 == HIGH) { //lampada apagada
    time_min = timeClient.getMinutes();
    time_min += 1;
    time_sec = seconds;
  }
  if (minutes == time_min && time_sec == seconds) {
    if (lead2 == LOW) {
      Serial.println(msg);
      gravarLog(msg);
      digitalWrite(LedBoard, HIGH);
      Gx = 0; Gy = 0; Gz = 0;
      Ax = 0; Ay = 0; Az = 0;
      return;
    } else {
      Gx = 0; Gy = 0; Gz = 0;
      Ax = 0; Ay = 0; Az = 0;
      return;
    }
  }
  digitalWrite(LedBoard, LOW);
}

/*-------------------------------------------------------------------------------------------
  Função para publicar o status do dispositivo
  -------------------------------------------------------------------------------------------*/
void publishStatus() {
  DynamicJsonDocument doc(1024);
  doc["id"] = 1;
  doc["status_connection"] = 1;
  char status_connection[128];
  serializeJson(doc, status_connection);
  client.publish("esp8266/status", status_connection);
}

/*-------------------------------------------------------------------------------------------
  Função para alterar o tempo de keep ALive
  -------------------------------------------------------------------------------------------*/
void setKeepALive() {
  //Pega o horário atual
  int horario = timeClient.getHours();
  int minutos = timeClient.getMinutes();
  int segundos = timeClient.getSeconds();
  //Serial.printf("%d:%d:%d \n",horario,minutos,segundos);
  //Serial.printf("cima keep= %d aux=%d \n",valKeepAlive,keepAliveAux);
  if (valKeepAlive != keepAliveAux) {
    Serial.printf("baixo keep= %d aux=%d \n", valKeepAlive, keepAliveAux);
    client.disconnect();
    client.setKeepAlive(valKeepAlive);
  }
  keepAliveAux = valKeepAlive;
}

/*-------------------------------------------------------------------------------------------
  Função setúp
  -------------------------------------------------------------------------------------------*/
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  pinMode(LedBoard, OUTPUT);
  //Mantém o led desligado quando inicia o dispositivo
  digitalWrite(LedBoard, HIGH);
  setup_wifi();
  delay(1000);

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }
  Serial.print("Heap: "); Serial.println(ESP.getFreeHeap());
  // carrega o certificado
  File cert = SPIFFS.open("/cert.der", "r");
  if (!cert) {
    Serial.println("Failed to open cert file");
  }
  else
    Serial.println("Success to open cert file");

  delay(1000);

  if (espClient.loadCertificate(cert))
    Serial.println("cert loaded");
  else
    Serial.println("cert not loaded");

  // carrega chave privada
  File private_key = SPIFFS.open("/private.der", "r");
  if (!private_key) {
    Serial.println("Failed to open private cert file");
  }
  else
    Serial.println("Success to open private cert file");

  delay(1000);

  if (espClient.loadPrivateKey(private_key))
    Serial.println("private key loaded");
  else
    Serial.println("private key not loaded");

  // carrega certificado CA
  File ca = SPIFFS.open("/ca.der", "r");
  if (!ca) {
    Serial.println("Failed to open ca ");
  }
  else
    Serial.println("Success to open ca");

  delay(1000);

  if (espClient.loadCACert(ca))
    Serial.println("ca loaded");
  else
    Serial.println("ca failed");
  delay(1000);

  // cria histórico
  Serial.println("Arquivos............");
  File historicoAtual = SPIFFS.open(arquivo, "a");
  if (!historicoAtual) {
    Serial.println("Falha ao abrir o historico Atual");
  }
  else
    Serial.println("historico Atual aberto com sucesso!");
  historicoAtual.close();

  //informação do armazenamento de arquivos
  FSInfo fs_info;
  SPIFFS.info(fs_info);

  Serial.println("\ninformações do armazenamento........");
  Serial.printf(" totalBytes=%u\n usedBytes =%u\n blockSize=%u\n"
                " pageSize=%u\n maxOpenFiles =%u\n maxPathlength =%u\n",
                fs_info.totalBytes,
                fs_info.usedBytes,
                fs_info.blockSize,
                fs_info.pageSize,
                fs_info.maxOpenFiles,
                fs_info.maxPathLength);

  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    Serial.print("\n" + dir.fileName());
    File f = dir.openFile("r");
    Serial.println("size: " + f.size());
  }
  Serial.println("");
  //cria e define tempo de threads
  threadPublish.setInterval(2000);
  threadPublish.onRun(publishStatus);
  threadSetKeep.setInterval(1000);
  threadSetKeep.onRun(setKeepALive);
  threadSerial.setInterval(1000);
  threadSerial.onRun(leituraDeDados);

  Serial.print("Heap: "); Serial.println(ESP.getFreeHeap());
}

/*-------------------------------------------------------------------------------------------
  Função loop
  -------------------------------------------------------------------------------------------*/
void loop() {
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  timeClient.update();
  if (threadPublish.shouldRun()) {
    threadPublish.run();
  }
  if (threadSetKeep.shouldRun()) {
    threadSetKeep.run();
  }
  if (threadSerial.shouldRun()) {
    threadSerial.run();
  }
  if (digitalRead(BUTTON) == LOW) {
    int lead = !digitalRead(LedBoard);
    digitalWrite(LedBoard, lead);
    Gx = 0; Gy = 0; Gz = 0;
    Ax = 0; Ay = 0; Az = 0;
    delay(300);
    Serial.println("Botão Pressionado");
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
