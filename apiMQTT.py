import json
import time
import pandas as pd
import threading
from datetime import datetime
import mysql.connector
import paho.mqtt.client as mqtt
import json

# Configurações MQTT
BROKER = "broker.emqx.io"  # Ou "127.0.0.1"
PORT = 1883  # Porta padrão do Mosquitto
USERNAME = "Armario"  # Não é necessário para configurações padrão
PASSWORD = "1235"  # Não é necessário para configurações padrão

SUBSCRIBE_TOPIC = "api/entrada"
PUBLISH_TOPIC = "api/saida"

with open("config.json", "r") as file:
    DB_CONFIG = json.load(file)

conn = mysql.connector.connect(**DB_CONFIG)
cursor = conn.cursor()

# DataFrame para armazenar dados recebidos do ESP32
dados_recebidos = pd.DataFrame(columns=["cpf", "acao", "tipo", "status", "sensor"])

aux = threading.Event()  # Usar threading.Event para controlar o envio contínuo
ultimo_cpf = ""
ultima_acao = ""
ultimo_tipo = ""
horario = ""

def validar_cpf(cpf: str) -> bool:
    cpf = ''.join(filter(str.isdigit, cpf)) # remove não numéricos

    if len(cpf) != 11 or cpf == cpf[0] * 11:
        return False
    
    soma1 = sum(int(cpf[i]) * (10 - i) for i in range(9))
    digito1 = (soma1 * 10 % 11) % 10

    soma2 = sum(int(cpf[i]) * (11 - i) for i in range(10))
    digito2 = (soma2 * 10  % 11) % 10

    return cpf[-2:] == f"{digito1}{digito2}"

# Callback ao conectar ao broker
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Conectado ao broker MQTT.")
        client.subscribe(SUBSCRIBE_TOPIC)
    else:
        print(f"Falha na conexão. Código: {rc}")

# Callback ao receber mensagens
def on_message(client, userdata, msg):
    global dados_recebidos, ultimo_cpf, ultima_acao, ultimo_tipo, horario

    try:
        # Decodificar mensagem JSON
        mensagem = json.loads(msg.payload.decode())
        print("Mensagem recebida:", mensagem)

        # Atualizar DataFrame com os dados recebidos
        dados_recebidos = pd.concat([
            dados_recebidos,
            pd.DataFrame([mensagem])
        ], ignore_index=True)

        # Verificar se a mensagem contém o comando "iniciar"
        comando = mensagem.get("comando", "")
        if comando == "iniciar":
            ultimo_cpf = mensagem.get("cpf", "")
            if(not validar_cpf(ultimo_cpf)):
                print("CPF inválido!")
                return
            ultima_acao = mensagem.get("acao", "")
            ultimo_tipo = mensagem.get("tipo", "")
            horario = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            aux.set()  # Ativar o evento para iniciar o envio contínuo
            print("Envio contínuo iniciado.")
        else:
            # Processar outras mensagens (como status ou sensor)
            status = mensagem.get("status")
            sensor = mensagem.get("sensor", "")
            if status:  # Parar o envio contínuo ao receber o status
                aux.clear()
                print(f"Envio contínuo interrompido. Estado da porta: {status}, sensor: {sensor}")
                # Inserir no banco de dados
                cursor.execute("""
                INSERT INTO mensagens (cpf, acao, tipo, status, sensor, horario)
                VALUES (%s, %s, %s, %s, %s, %s)
                """, (
                ultimo_cpf,
                ultima_acao,
                ultimo_tipo,
                status,
                sensor,
                horario
                ))
                conn.commit()
            
    except json.JSONDecodeError:
        print("Erro ao decodificar mensagem JSON.")

# Função para publicar dados no ESP32
def enviar_comando(cpf, acao, tipo,horario):
    mensagem = {
        "cpf": cpf,
        "acao": acao,
        "tipo": tipo,
        "horario": horario
    }
    client.publish(PUBLISH_TOPIC, json.dumps(mensagem))
    print("Comando enviado:", mensagem)

# Thread para envio contínuo de mensagens
def envio_continuo():
    while True:
        aux.wait()  # Aguarda até que o evento seja ativado
        enviar_comando(ultimo_cpf, ultima_acao, ultimo_tipo,horario)
        time.sleep(2)

# Inicialização do cliente MQTT
client = mqtt.Client()
client.username_pw_set(USERNAME, PASSWORD)
client.on_connect = on_connect
client.on_message = on_message

# Conectar ao broker
print("Conectando ao broker MQTT...")
client.connect(BROKER, PORT, 60)

# Iniciar thread de envio contínuo
thread = threading.Thread(target=envio_continuo, daemon=True)
thread.start()

# Loop principal
try:
    client.loop_forever()
except KeyboardInterrupt:
    print("Encerrando a aplicação...")
    client.disconnect()
    conn.close()
