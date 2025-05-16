#include "DHT.h" //Permite usar los sensores de temperatura y humedad
#include <WiFi.h> //Es la librería para conectar el ESP32 a una red WiFi
#include <WebServer.h> //Permite crear un servidor web que interprete las peticiones en el ESP32 

// Define el pin digital al que está conectado el sensor DHT11
#define DHTPIN 15

// Define el tipo de sensor (DHT11 en este caso)
#define DHTTYPE DHT11

// Crea un objeto 'dht' del tipo DHT, utilizando el pin y tipo definidos
DHT dht(DHTPIN, DHTTYPE);

// Define los pines de control para la bomba de agua
#define IN1 18    // Pin de control IN1 del puente H (activo para encender la bomba)
#define IN2 25    // Pin de control IN2 del puente H

// Define el pin que controla el ventilador
#define FAN_PIN 26

// Define el pin analógico conectado al sensor de humedad de suelo (MH-Sensor-Series)
#define SOIL_PIN 34

// Variables para almacenar el estado de la bomba y el ventilador
// Se encargan de definir también si el sistema está en modo manual o automático
bool bombaEncendida = false;
bool ventiladorEncendido = false;
bool modoAutomatico = true;  // Por defecto, el modo automático está activado

// Datos del sensor, se guardan las lecturas del sensor
float temperatura = 0.0;
float humedadAire = 0.0;
float humedadSueloPorcentaje = 0.0;

// Configuración de red WiFi - Se modifican los datos según las credenciales, en este caso, del teléfono de Juana
const char* ssid = "Juana";          // Nombre de la red WiFi
const char* password = "123456789"; // Contraseña de la red WiFi

// Crea un servidor web en el puerto 80
WebServer server(80);

// Función de configuración: se ejecuta una sola vez al iniciar
void setup() {
  // Primero se inicia la comunicación serial para la depuración
  Serial.begin(115200);  // Se aumenta la velocidad para mejor depuración
  delay(1000);  // Una breve pausa para estabilizar

  Serial.println("\n\n======================================");
  Serial.println("Sistema de riego y ventilación automático");
  Serial.println("======================================");

  // Configura los pines de salida para la bomba y ventilador
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  // Asegurarse de que todo está apagado al inicio
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(FAN_PIN, LOW);
  
  // Inicia la comunicación con el sensor DHT11
  dht.begin();
  Serial.println("Sensor DHT iniciado");

  // Intentar conectar a la red WiFi
  conectarWiFi();

  // Configurar las rutas del servidor web
  configurarServidor();

  Serial.println("Servidor web iniciado");
  
  // Muestra la información sobre cómo conectarse
  mostrarInformacionConexion();
}

// Función principal que se repite constantemente
void loop() {
  // Maneja las peticiones del cliente web
  server.handleClient();

  // Lee los sensores y actualiza la interfaz cada 2 segundos, también ejecuta el control automático si está activado y muestra los datos 
  static unsigned long ultimaLectura = 0;
  unsigned long tiempoActual = millis();
  
  if (tiempoActual - ultimaLectura >= 2000) {
    ultimaLectura = tiempoActual;
    
    // Lee y verifica los sensores
    if (!leerSensores()) {
      Serial.println("Error al leer sensores - omitiendo este ciclo");
      return;
    }
    
    // Solo controla automáticamente si el modo automático está activado
    if (modoAutomatico) {
      controlAutomatico();
    }
    
    // Imprimir valores en el monitor serial
    imprimirDatosSensores();
  }
  
  // Mostrar información de conexión periódicamente
  static unsigned long ultimaInfoConexion = 0;
  if (tiempoActual - ultimaInfoConexion >= 60000) { // Cada minuto
    ultimaInfoConexion = tiempoActual;
    mostrarInformacionConexion();
  }

  // Pequeña pausa para estabilidad
  delay(10);
}

// Función para mostrar información de la conexión 
void mostrarInformacionConexion() {
  Serial.println("\n======================================");
  Serial.println("INFORMACIÓN DE CONEXIÓN:");
  Serial.print("Estado WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("CONECTADO");
    Serial.print("Conectado a la red: ");
    Serial.println(ssid);
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Fuerza de señal (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println("\nPara acceder al panel de control:");
    Serial.print("Abre tu navegador web y visita: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("DESCONECTADO");
    Serial.println("Intentando reconectar...");
    WiFi.reconnect();
  }
  Serial.println("======================================\n");
}

// Función para conectar a la red WiFi
void conectarWiFi() {
  Serial.print("Conectando a ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);  // Modo estación
  WiFi.begin(ssid, password); //Se conecta a WiFi

  // Espera hasta que se conecte con un timeout, espera hasta 20 segundos
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFalló la conexión WiFi. Reiniciando...");
    delay(3000);
    ESP.restart();
    return;
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
}

// Configurar las rutas del servidor web
void configurarServidor() {
  // Página principal
  server.on("/", HTTP_GET, handleRoot);
  
  // Obtener datos de los sensores en formato JSON
  server.on("/datos", HTTP_GET, handleDatos);
  
  // Rutas para controlar la bomba
  server.on("/bomba/on", HTTP_GET, handleBombaOn);
  server.on("/bomba/off", HTTP_GET, handleBombaOff);
  
  // Rutas para controlar el ventilador
  server.on("/ventilador/on", HTTP_GET, handleVentiladorOn);
  server.on("/ventilador/off", HTTP_GET, handleVentiladorOff);
  
  // Rutas para cambiar el modo (automático/manual)
  server.on("/modo/auto", HTTP_GET, handleModoAuto);
  server.on("/modo/manual", HTTP_GET, handleModoManual);
  
  // Iniciar el servidor
  server.begin();
}

// Leer todos los sensores
bool leerSensores() {
  // Lee la temperatura del aire desde el sensor DHT11 (en grados Celsius)
  float tempLeida = dht.readTemperature();

  // Lee la humedad del aire (porcentaje relativo)
  float humedadLeida = dht.readHumidity();

  // Verifica si hubo error al leer temperatura o humedad del DHT11
  if (isnan(tempLeida) || isnan(humedadLeida)) {
    Serial.println("Error al leer el sensor DHT11");
    return false;
  }
  
  // Actualiza valores solo si son válidos
  temperatura = tempLeida;
  humedadAire = humedadLeida;

  // Lee el valor analógico de humedad del suelo con protección
  int humedadSuelo = analogRead(SOIL_PIN);
  
  // Verifica si el valor de humedad del suelo está en rango válido
  if (humedadSuelo < 0 || humedadSuelo > 4095) {
    Serial.println("Error al leer sensor de humedad del suelo");
    return false;
  }

  // Convierte la lectura analógica a un porcentaje de humedad
  humedadSueloPorcentaje = ((4095.0 - humedadSuelo) / 4095.0) * 100.0;
  
  return true;
}

// Control automático basado en la temperatura
void controlAutomatico() {
  // Si la temperatura es mayor o igual a 28°C, se activan la bomba y el ventilador
  if (temperatura >= 28.0) {
    Serial.println("Temperatura alta - Activando bomba y ventilador");
    encenderBomba();
    encenderVentilador();
  } else {
    // Si la temperatura es normal, apaga los sistemas
    Serial.println("Temperatura normal - Apagando bomba y ventilador");
    apagarBomba();
    apagarVentilador();
  }
}

// Imprime los datos de los sensores en el monitor serial
void imprimirDatosSensores() {
  // Muestra la temperatura del aire
  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.println(" °C");

  // Muestra la humedad del aire
  Serial.print("Humedad del aire: ");
  Serial.print(humedadAire);
  Serial.println(" %");

  // Muestra la humedad del suelo convertida a porcentaje
  Serial.print("Humedad del suelo: ");
  Serial.print(humedadSueloPorcentaje);
  Serial.println(" %");
  
  // Muestra el estado del modo (automático o manual)
  Serial.print("Modo: ");
  Serial.println(modoAutomatico ? "Automático" : "Manual");
  
  // Muestra el estado de la bomba y el ventilador
  Serial.print("Bomba: ");
  Serial.println(bombaEncendida ? "Encendida" : "Apagada");
  
  Serial.print("Ventilador: ");
  Serial.println(ventiladorEncendido ? "Encendido" : "Apagado");

  Serial.println("-----------------------------");
}

// Funciones para controlar la bomba
void encenderBomba() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  bombaEncendida = true;
  Serial.println("Bomba ENCENDIDA");
}

void apagarBomba() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  bombaEncendida = false;
  Serial.println("Bomba APAGADA");
}

// Funciones para controlar el ventilador
void encenderVentilador() {
  digitalWrite(FAN_PIN, HIGH);
  ventiladorEncendido = true;
  Serial.println("Ventilador ENCENDIDO");
}

void apagarVentilador() {
  digitalWrite(FAN_PIN, LOW);
  ventiladorEncendido = false;
  Serial.println("Ventilador APAGADO");
}

// Manejadores para las rutas del servidor web

// Página principal (HTML) - Estación Climática
void handleRoot() { //Muestra la página web con botones
  String html = "<!DOCTYPE html><html>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Estación Climática</title>"
    "<style>"
    "body{font-family:Arial;margin:0;padding:20px;background:#f0f0f0;color:#333;}"
    ".container{max-width:600px;margin:0 auto;background:white;border-radius:10px;padding:20px;box-shadow:0 0 10px rgba(0,0,0,0.1);}"
    "h1{color:#2196F3;text-align:center;}"
    ".sensor-data{background:#e8f5e9;padding:15px;border-radius:8px;margin-bottom:20px;}"
    ".control-panel{background:#e3f2fd;padding:15px;border-radius:8px;margin-bottom:20px;}"
    ".button-group{display:flex;justify-content:space-between;margin-bottom:15px;}"
    "button{background:#2196F3;color:white;border:none;padding:10px 15px;border-radius:5px;cursor:pointer;flex-grow:1;margin:0 5px;font-weight:bold;}"
    "button.active{background:#4CAF50;}"
    "button.inactive{background:#f44336;}"
    "button:disabled{background:#ccc;cursor:not-allowed;}"
    ".mode-toggle{display:flex;justify-content:center;margin-bottom:20px;}"
    ".mode-toggle button{width:48%;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>Estación Climática</h1>"
    
    "<div class='sensor-data'>"
    "<h2>Datos de Sensores</h2>"
    "<p><strong>Temperatura:</strong> <span id='temperatura'>--</span> °C</p>"
    "<p><strong>Humedad del Aire:</strong> <span id='humedad-aire'>--</span>%</p>"
    "<p><strong>Humedad del Suelo:</strong> <span id='humedad-suelo'>--</span>%</p>"
    "</div>"
    
    "<div class='control-panel'>"
    "<h2>Panel de Control</h2>"
    
    "<div class='mode-toggle'>"
    "<button id='modo-auto' onclick='cambiarModo(\"auto\")'>Modo Automático</button>"
    "<button id='modo-manual' onclick='cambiarModo(\"manual\")'>Modo Manual</button>"
    "</div>"
    
    "<div class='button-group'>"
    "<button id='bomba-on' onclick='controlarBomba(\"on\")'>Encender Bomba</button>"
    "<button id='bomba-off' onclick='controlarBomba(\"off\")'>Apagar Bomba</button>"
    "</div>"
    
    "<div class='button-group'>"
    "<button id='ventilador-on' onclick='controlarVentilador(\"on\")'>Encender Ventilador</button>"
    "<button id='ventilador-off' onclick='controlarVentilador(\"off\")'>Apagar Ventilador</button>"
    "</div>"
    "</div>"
    "</div>"

    "<script>"
    "let modoAutomatico=true;"
    "let bombaEncendida=false;"
    "let ventiladorEncendido=false;"
    
    "function actualizarInterfaz(){"
    "document.getElementById('modo-auto').className=modoAutomatico?'active':'';"
    "document.getElementById('modo-manual').className=!modoAutomatico?'active':'';"
    "document.getElementById('bomba-on').disabled=modoAutomatico;"
    "document.getElementById('bomba-off').disabled=modoAutomatico;"
    "document.getElementById('ventilador-on').disabled=modoAutomatico;"
    "document.getElementById('ventilador-off').disabled=modoAutomatico;"
    "document.getElementById('bomba-on').className=bombaEncendida?'active':'';"
    "document.getElementById('bomba-off').className=!bombaEncendida?'active':'';"
    "document.getElementById('ventilador-on').className=ventiladorEncendido?'active':'';"
    "document.getElementById('ventilador-off').className=!ventiladorEncendido?'active':'';"
    "}"
    
    "function obtenerDatos(){"
    "fetch('/datos')"
    ".then(response=>response.json())"
    ".then(data=>{"
    "document.getElementById('temperatura').textContent=data.temperatura.toFixed(1);"
    "document.getElementById('humedad-aire').textContent=data.humedadAire.toFixed(1);"
    "document.getElementById('humedad-suelo').textContent=data.humedadSuelo.toFixed(1);"
    "modoAutomatico=data.modoAutomatico;"
    "bombaEncendida=data.bombaEncendida;"
    "ventiladorEncendido=data.ventiladorEncendido;"
    "actualizarInterfaz();"
    "})"
    ".catch(error=>console.error('Error:',error));"
    "}"
    
    "function cambiarModo(modo){"
    "fetch(`/modo/${modo}`)"
    ".then(response=>response.json())"
    ".then(data=>{"
    "modoAutomatico=data.modoAutomatico;"
    "actualizarInterfaz();"
    "})"
    ".catch(error=>console.error('Error:',error));"
    "}"
    
    "function controlarBomba(accion){"
    "if(modoAutomatico)return;"
    "fetch(`/bomba/${accion}`)"
    ".then(response=>response.json())"
    ".then(data=>{"
    "bombaEncendida=data.bombaEncendida;"
    "actualizarInterfaz();"
    "})"
    ".catch(error=>console.error('Error:',error));"
    "}"
    
    "function controlarVentilador(accion){"
    "if(modoAutomatico)return;"
    "fetch(`/ventilador/${accion}`)"
    ".then(response=>response.json())"
    ".then(data=>{"
    "ventiladorEncendido=data.ventiladorEncendido;"
    "actualizarInterfaz();"
    "})"
    ".catch(error=>console.error('Error:',error));"
    "}"
    
    "setInterval(obtenerDatos,2000);"
    "obtenerDatos();"
    "</script>"
    "</body></html>";
  
  server.send(200, "text/html", html);
}

// Enviar datos de los sensores en formato JSON
//Son respuestas JSON con los valores actuales de los sensores y estados de la bomba, el ventilador y el modo automático. 
void handleDatos() {
  String json = "{";
  json += "\"temperatura\": " + String(temperatura) + ",";
  json += "\"humedadAire\": " + String(humedadAire) + ",";
  json += "\"humedadSuelo\": " + String(humedadSueloPorcentaje) + ",";
  json += "\"bombaEncendida\": " + String(bombaEncendida ? "true" : "false") + ",";
  json += "\"ventiladorEncendido\": " + String(ventiladorEncendido ? "true" : "false") + ",";
  json += "\"modoAutomatico\": " + String(modoAutomatico ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

// Manejadores para controlar la bomba, es decir, encender o apagar la bomba, se confirma en un mensaje
void handleBombaOn() {
  if (!modoAutomatico) {
    encenderBomba();
  }
  
  String json = "{\"bombaEncendida\": " + String(bombaEncendida ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleBombaOff() {
  if (!modoAutomatico) {
    apagarBomba();
  }
  
  String json = "{\"bombaEncendida\": " + String(bombaEncendida ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// Manejadores para controlar el ventilador, es decir, encender o apagar el ventilador, se confirma en un mensaje
void handleVentiladorOn() {
  if (!modoAutomatico) {
    encenderVentilador();
  }
  
  String json = "{\"ventiladorEncendido\": " + String(ventiladorEncendido ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleVentiladorOff() {
  if (!modoAutomatico) {
    apagarVentilador();
  }
  
  String json = "{\"ventiladorEncendido\": " + String(ventiladorEncendido ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// Manejadores para cambiar el modo
// Se activa o desactiva el modo automático del sistema
void handleModoAuto() {
  modoAutomatico = true;
  
  String json = "{\"modoAutomatico\": true}";
  server.send(200, "application/json", json);
}

void handleModoManual() {
  modoAutomatico = false;
  
  String json = "{\"modoAutomatico\": false}";
  server.send(200, "application/json", json);
}