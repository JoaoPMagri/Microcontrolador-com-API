#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
//#include <SD.h>

// Configurações Wifi
const char* ssid = "jpm_notebook";
const char* password = "12345678";

// Configurações MQTT
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_user = "Armario";
const char* mqtt_pass = "1235";

const char* subscribe_topic = "api/saida";
const char* publish_topic = "api/entrada";

// Objetos Wi-Fi e MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// Configurações do servo e sensor
const int angulo_servoMotor1 = 0;
const int angulo_servoMotor2 = 90;
const int distancia_sensor = 12;

const int triggerPin = 16;
const int echoPin = 17;
const int servoPin = 21;

Servo servoMotor;

// Configurações do cartão SD
//const int sdChipSelect = 5; // Pino CS do módulo SD

// Variáveis de estado
String id = "";
String acao = "";
String tipo = "";
String horario = "";
bool status = false;

// Função para inicializar o SD
/*void inicializarSD() {
    if (!SD.begin(sdChipSelect)) {
        Serial.println("Falha ao inicializar o cartão SD.");
        while (true); // Trava o sistema se falhar
    }
    Serial.println("Cartão SD inicializado com sucesso.");
}

// Função para salvar dados no SD
void salvarNoSD(const String& mensagem) {
    File arquivo = SD.open("/dados.txt", FILE_APPEND);
    if (arquivo) {
        arquivo.println(mensagem);
        arquivo.close();
        Serial.println("Dados salvos no SD: " + mensagem);
    } else {
        Serial.println("Falha ao abrir arquivo no SD.");
    }
}*/

// Função para medir a distância
float medirDistancia() {
    digitalWrite(triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin, LOW);

    long duracao = pulseIn(echoPin, HIGH);
    float distancia = duracao * 0.034 / 2;
    Serial.print("\n");
    Serial.println(distancia);
    return distancia;
}

// Função para tratar mensagens recebidas
void callback(char* topic, byte* payload, unsigned int length) {
    String mensagem = "";
    for (int i = 0; i < length; i++) {
        mensagem += (char)payload[i];
    }
    Serial.println("Mensagem recebida: " + mensagem);

    //salvarNoSD("Recebido: " + mensagem); // Salvar mensagem recebida no SD

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, mensagem);
    if (error) {
        Serial.println("Erro ao parsear JSON recebido.");
        return;
    }
    
    id = doc["id"].as<String>();
    acao = doc["acao"].as<String>();
    tipo = doc["tipo"].as<String>();
    horario = doc["horario"].as<String>();

    //Serial.println(acao + " " + status);

    float distancia = medirDistancia();
    if (tipo == "entregador") {
        if (acao == "abrir" && !status && distancia >= distancia_sensor) {
            status = true;
            servoMotor.write(angulo_servoMotor2);
            enviar_dados_api(distancia, "True");
        } else if (acao == "fechar" && status && distancia < distancia_sensor) {
            status = false;
            servoMotor.write(angulo_servoMotor1);
            enviar_dados_api(distancia, "True");
        }
        else{
          Serial.println("Não cumpre os requisitos");
          enviar_dados_api(distancia, "False");
        }
    } else if (tipo == "cliente") {
        if (acao == "abrir" && !status && distancia < distancia_sensor) {
            status = true;
            servoMotor.write(angulo_servoMotor2);
            enviar_dados_api(distancia, "True");
        } else if (acao == "fechar" && status && distancia >= distancia_sensor) {
            status = false;
            servoMotor.write(angulo_servoMotor1);
            enviar_dados_api(distancia, "True");
        }
        else{
          Serial.println("Não cumpre os requisitos");
          enviar_dados_api(distancia, "False");
        }
    }
}

// Conectar ao Wi-Fi
void setup_wifi() {
    WiFi.begin(ssid, password);
    Serial.print("Conectando ao Wifi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("Wifi conectado");
}

// Reconectar ao MQTT Broker
void reconnect() {
    while (!client.connected()) {
        if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
            client.subscribe(subscribe_topic);
        } else {
            Serial.print("Falha ao conectar. Código: ");
            Serial.println(client.state());
            delay(2000);
        }
    }
}

// Enviar dados para a API
void enviar_dados_api(float distancia, String sucesso) {
    DynamicJsonDocument doc(256);
    doc["sensor"] = distancia;
    doc["status"] = status ? "aberta" : "fechada";
    doc["comando"] = "parar";
    String mensagem;
    serializeJson(doc, mensagem);

    client.publish(publish_topic, mensagem.c_str());
    //salvarNoSD("Enviado: " + mensagem); // Salvar mensagem enviada no SD
    Serial.println("Dados enviados para a API: " + mensagem);
}

void setup() {
    pinMode(triggerPin, OUTPUT);
    pinMode(echoPin, INPUT);

    servoMotor.attach(servoPin, 500, 2400);
    servoMotor.write(angulo_servoMotor1);
    Serial.begin(9600);
    delay(200);

    setup_wifi(); 
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
}
