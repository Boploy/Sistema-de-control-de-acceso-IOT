// ============================================================
//  Sistema de Control de Acceso IoT
//  CUCEI — Universidad de Guadalajara
//
//  DIP orden físico: {36,39,32,33,25,26,27,13,4,5}
//  bit0=DIP2-sw1  bit1=DIP2-sw2  bit2..9=DIP8-sw1..sw8
//
//  Contraseña inicial:
//    DIP-2: [ 1 1 ]
//    DIP-8: [ 1 1 0 0 0 0 1 1 ]
//
//  Fields ThingSpeak:
//    F1: Estado sistema   0=apagado 1=activo 2=bloqueado
//    F2: Ultimo evento    1=ok 2=denegado 3=bloqueado 4=desbloqueado
//                         5=reset 6=limite_cambiado 7=pwd_cambiada
//                         8=admin_on 9=admin_off
//    F3: Intentos fallidos
//    F4: Ultimo intento DIP (numero decimal)
//    F5: Limite intentos
//    F6: Comando remoto (entrada)
//    F7: Captcha activo   0=no 1=si
//    F8: Modo admin       0=no 1=si
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ThingSpeak.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define THINGSPEAK_CHANNEL_ID 0L
#define THINGSPEAK_WRITE_KEY "YOUR_THINGSPEAK_WRITE_KEY"
#define THINGSPEAK_READ_KEY "YOUR_THINGSPEAK_READ_KEY"
#endif

// ─── WiFi y ThingSpeak ───────────────────────────────────────────
const char* ssid       = WIFI_SSID;
const char* password   = WIFI_PASSWORD;
const long  CHANNEL_ID = THINGSPEAK_CHANNEL_ID;
const char* WRITE_KEY  = THINGSPEAK_WRITE_KEY;
const char* READ_KEY   = THINGSPEAK_READ_KEY;

WiFiClient client;

// ─── Pines DIP ───────────────────────────────────────────────────
const int DIP[10] = {36, 39, 32, 33, 25, 26, 27, 13, 4, 5};

// ─── Botones ─────────────────────────────────────────────────────
const int BTN_CONFIRMAR  = 16;
const int BTN_ADMIN      = 17;
const int BTN_ENERGIZADO = 23;

// ─── LEDs ────────────────────────────────────────────────────────
const int LED_VERDE    = 19;
const int LED_ROJO     = 18;
const int LED_AMARILLO = 21;

// ─── Potenciómetro captcha (ADC1 — funciona con WiFi) ────────────
const int POT = 34;

// ─── Contraseña ──────────────────────────────────────────────────
const int PWD_BITS[10] = {1, 1, 1, 1, 0, 0, 0, 0, 1, 1};

int bitsAEntero(const int bits[10]) {
  int val = 0;
  for (int i = 0; i < 10; i++) {
    if (bits[i]) val |= (1 << i);
  }
  return val;
}

int contrasenaActiva = 0;
int limiteIntentos   = 3;
int intentosFallidos = 0;

// ─── Estados ─────────────────────────────────────────────────────
bool sistemaActivo    = false;
bool bloqueado        = false;
bool modoAdmin        = false;
bool captchaPendiente = false;
bool captchaFase1     = false;
bool captchaOk        = false;

// ─── Evento pendiente para ThingSpeak ────────────────────────────
int    ultimoEvento           = 0;
int    ultimoIntentoDIP       = 0;
bool   hayEvento              = false;
String ultimoComandoEjecutado = "";

// ─── Timers ──────────────────────────────────────────────────────
const unsigned long INTERVALO_TS   = 15000;
const unsigned long INTERVALO_CMD  = 15000;
const unsigned long TIMEOUT_WIFI   = 8000;
const unsigned long RECONEX_ESPERA = 5000;

unsigned long ultimoEnvio    = 0;
unsigned long ultimoComando  = 0;
unsigned long ultimoReconect = 0;

// ─── Antirrebote ─────────────────────────────────────────────────
bool lastConfirmar  = HIGH;
bool lastAdmin      = HIGH;
bool lastEnergizado = HIGH;

// ─── Codigos de evento para Field 2 ──────────────────────────────
#define EVT_ACCESO_OK        1
#define EVT_DENEGADO         2
#define EVT_BLOQUEADO        3
#define EVT_DESBLOQUEADO     4
#define EVT_RESET            5
#define EVT_LIMITE_CAMBIADO  6
#define EVT_PWD_CAMBIADA     7
#define EVT_ADMIN_ON         8
#define EVT_ADMIN_OFF        9

// ─── Prototipos ──────────────────────────────────────────────────
void reconectar();
int  leerDIP();
void imprimirDIP(int valor);
void imprimirContrasena();
void leerBotones();
void verificarAcceso();
void leerCaptcha();
void registrarEvento(int codigoEvento);
void enviarThingSpeak();
void leerComandoRemoto();
void apagarTodosLeds();
void parpadear(int pin, int veces, int ms);

// ─────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(LED_VERDE,      OUTPUT);
  pinMode(LED_ROJO,       OUTPUT);
  pinMode(LED_AMARILLO,   OUTPUT);
  pinMode(BTN_CONFIRMAR,  INPUT_PULLUP);
  pinMode(BTN_ADMIN,      INPUT_PULLUP);
  pinMode(BTN_ENERGIZADO, INPUT_PULLUP);

  for (int i = 0; i < 10; i++) {
    pinMode(DIP[i], INPUT);
  }

  apagarTodosLeds();
  contrasenaActiva = bitsAEntero(PWD_BITS);

  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  unsigned long inicioWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicioWifi < TIMEOUT_WIFI) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado: " + WiFi.localIP().toString());
    ThingSpeak.begin(client);
    int limiteRemoto = ThingSpeak.readIntField(CHANNEL_ID, 5, READ_KEY);
    if (ThingSpeak.getLastReadStatus() == 200 && limiteRemoto > 0 && limiteRemoto <= 10) {
      limiteIntentos = limiteRemoto;
      Serial.print("Limite cargado desde ThingSpeak: ");
      Serial.println(limiteIntentos);
    }
    // Cargar ultimo comando para no re-ejecutarlo al arrancar
    String cmdActual = ThingSpeak.readStringField(CHANNEL_ID, 6, READ_KEY);
    if (ThingSpeak.getLastReadStatus() == 200) {
      cmdActual.trim();
      ultimoComandoEjecutado = cmdActual;
    }
  } else {
    Serial.println("\nSin WiFi — modo local activo");
  }

  Serial.print("Contrasena activa: ");
  imprimirContrasena();
  Serial.println("Sistema listo. Presiona boton energizado para iniciar.");
  Serial.println("Ingrese la contraseña y presione boton CONFIRMAR cuando esté listo.");
}

// ─────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────
void loop() {
  reconectar();
  leerBotones();

  if (captchaPendiente) leerCaptcha();

  if (WiFi.status() == WL_CONNECTED) {
    unsigned long ahora = millis();

    if (ahora - ultimoComando >= INTERVALO_CMD) {
      leerComandoRemoto();
      ultimoComando = ahora;
    }
    if (ahora - ultimoEnvio >= INTERVALO_TS) {
      enviarThingSpeak();
      ultimoEnvio = ahora;
    }
  }
}

// ─────────────────────────────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────────────────────────────
void reconectar() {
  if (WiFi.status() != WL_CONNECTED &&
      millis() - ultimoReconect > RECONEX_ESPERA) {
    WiFi.begin(ssid, password);
    ultimoReconect = millis();
  }
}

// ─────────────────────────────────────────────────────────────────
//  Lectura DIP — HIGH=1, LOW=0
// ─────────────────────────────────────────────────────────────────
int leerDIP() {
  int valor = 0;
  for (int i = 0; i < 10; i++) {
    if (digitalRead(DIP[i]) == HIGH) valor |= (1 << i);
  }
  return valor;
}

// ─────────────────────────────────────────────────────────────────
//  Imprimir DIP como 0s y 1s con ceros a la izquierda
// ─────────────────────────────────────────────────────────────────
void imprimirDIP(int valor) {
  Serial.print("DIP-2 [ ");
  Serial.print((valor >> 0) & 1); Serial.print(" ");
  Serial.print((valor >> 1) & 1);
  Serial.print(" ]  DIP-8 [ ");
  Serial.print((valor >> 2) & 1); Serial.print(" ");
  Serial.print((valor >> 3) & 1); Serial.print(" ");
  Serial.print((valor >> 4) & 1); Serial.print(" ");
  Serial.print((valor >> 5) & 1); Serial.print(" ");
  Serial.print((valor >> 6) & 1); Serial.print(" ");
  Serial.print((valor >> 7) & 1); Serial.print(" ");
  Serial.print((valor >> 8) & 1); Serial.print(" ");
  Serial.print((valor >> 9) & 1);
  Serial.println(" ]");
}

void imprimirContrasena() {
  imprimirDIP(contrasenaActiva);
}

// ─────────────────────────────────────────────────────────────────
//  Registrar evento — guarda en memoria, el loop lo manda
// ─────────────────────────────────────────────────────────────────
void registrarEvento(int codigoEvento) {
  ultimoEvento = codigoEvento;
  if (codigoEvento == EVT_ACCESO_OK || codigoEvento == EVT_DENEGADO) {
    ultimoIntentoDIP = leerDIP();
  }
  hayEvento = true;
}

// ─────────────────────────────────────────────────────────────────
//  Enviar todo a ThingSpeak — una sola escritura cada 15s
// ─────────────────────────────────────────────────────────────────
void enviarThingSpeak() {
  int estadoSistema = bloqueado ? 2 : (sistemaActivo ? 1 : 0);
  ThingSpeak.setField(1, estadoSistema);
  ThingSpeak.setField(2, ultimoEvento);
  ThingSpeak.setField(3, intentosFallidos);
  ThingSpeak.setField(4, ultimoIntentoDIP);
  ThingSpeak.setField(5, limiteIntentos);
  ThingSpeak.setField(7, captchaPendiente ? 1 : 0);
  ThingSpeak.setField(8, modoAdmin        ? 1 : 0);
  ThingSpeak.writeFields(CHANNEL_ID, WRITE_KEY);
  hayEvento = false;
}

// ─────────────────────────────────────────────────────────────────
//  Botones
// ─────────────────────────────────────────────────────────────────
void leerBotones() {
  bool estadoConf = digitalRead(BTN_CONFIRMAR);
  bool estadoAdm  = digitalRead(BTN_ADMIN);
  bool estadoEn   = digitalRead(BTN_ENERGIZADO);

  // ── Energizado: toggle ON/OFF ──
  if (estadoEn == LOW && lastEnergizado == HIGH) {
    delay(50);
    sistemaActivo = !sistemaActivo;
    apagarTodosLeds();
    if (sistemaActivo) {
      Serial.println("Sistema ACTIVADO");
      parpadear(LED_VERDE, 2, 200);
    } else {
      Serial.println("Sistema DESACTIVADO");
      parpadear(LED_ROJO, 2, 200);
    }
  }
  lastEnergizado = estadoEn;

  // ── Admin: toggle modo admin ──
  if (estadoAdm == LOW && lastAdmin == HIGH) {
    delay(50);
    if (!bloqueado) {
      modoAdmin = !modoAdmin;
      if (modoAdmin) {
        Serial.println("Modo ADMIN ON — pruebas sin penalizacion");
        parpadear(LED_AMARILLO, 3, 150);
        registrarEvento(EVT_ADMIN_ON);
      } else {
        intentosFallidos = 0;
        captchaPendiente = false;
        captchaOk        = false;
        apagarTodosLeds();
        Serial.println("Modo ADMIN OFF — volviendo a modo normal");
        parpadear(LED_VERDE, 2, 200);
        registrarEvento(EVT_ADMIN_OFF);
      }
    } else {
      Serial.println("Sistema bloqueado — admin deshabilitado");
      parpadear(LED_AMARILLO, 1, 300);
    }
  }
  lastAdmin = estadoAdm;

  // ── Confirmar: verificar contrasena ──
  if (estadoConf == LOW && lastConfirmar == HIGH) {
    delay(50);
    if (!sistemaActivo) {
      Serial.println("Sistema no activo — presiona boton energizado primero");
      parpadear(LED_ROJO, 1, 300);
    } else if (bloqueado) {
      Serial.println("Sistema bloqueado — desbloquea desde ThingSpeak");
      parpadear(LED_AMARILLO, 1, 300);
    } else if (captchaPendiente && !captchaOk) {
      Serial.println("Resuelve el captcha primero");
      parpadear(LED_ROJO, 2, 300);
    } else {
      verificarAcceso();
    }
  }
  lastConfirmar = estadoConf;
}

// ─────────────────────────────────────────────────────────────────
//  Verificacion de acceso
// ─────────────────────────────────────────────────────────────────
void verificarAcceso() {
  int dipActual = leerDIP();
  Serial.print("Leido:     "); imprimirDIP(dipActual);

  apagarTodosLeds();
  captchaOk = false;

  if (dipActual == contrasenaActiva) {
    intentosFallidos = 0;
    captchaPendiente = false;
    Serial.println(modoAdmin ? "PRUEBA — contrasena correcta" : "ACCESO CONCEDIDO");
    digitalWrite(LED_VERDE, HIGH);
    delay(2000);
    digitalWrite(LED_VERDE, LOW);
    if (!modoAdmin) registrarEvento(EVT_ACCESO_OK);

  } else {
    digitalWrite(LED_ROJO, HIGH);
    delay(2000);
    digitalWrite(LED_ROJO, LOW);

    if (modoAdmin) {
      Serial.println("PRUEBA — contrasena incorrecta, sin penalizacion");
    } else {
      intentosFallidos++;
      Serial.print("ACCESO DENEGADO — intento ");
      Serial.print(intentosFallidos);
      Serial.print(" de ");
      Serial.println(limiteIntentos);
      registrarEvento(EVT_DENEGADO);

      if (intentosFallidos >= limiteIntentos) {
        bloqueado        = true;
        captchaPendiente = false;
        digitalWrite(LED_AMARILLO, HIGH);
        registrarEvento(EVT_BLOQUEADO);
        Serial.println("SISTEMA BLOQUEADO — desbloquea desde ThingSpeak");
      } else {
        captchaPendiente = true;
        captchaFase1     = false;
        Serial.print("Intentos restantes: ");
        Serial.println(limiteIntentos - intentosFallidos);
        Serial.println("Captcha: sube pot al 80%, luego baja al 20%");
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────
//  Captcha fisico
// ─────────────────────────────────────────────────────────────────
void leerCaptcha() {
  int val = map(analogRead(POT), 0, 4095, 0, 100);

  if (!captchaFase1 && val >= 80) {
    captchaFase1 = true;
    Serial.println("Captcha fase 1 OK — ahora baja al 20%");
  }
  if (captchaFase1 && val <= 20) {
    captchaOk        = true;
    captchaPendiente = false;
    Serial.println("Captcha resuelto — ya puedes intentar de nuevo");
    parpadear(LED_VERDE, 3, 100);
  }
}

// ─────────────────────────────────────────────────────────────────
//  ThingSpeak — leer comando remoto (Field 6)
// ─────────────────────────────────────────────────────────────────
void leerComandoRemoto() {
  String cmd = ThingSpeak.readStringField(CHANNEL_ID, 6, READ_KEY);
  if (ThingSpeak.getLastReadStatus() != 200 || cmd == "") return;
  cmd.trim();
  if (cmd == "-1" || cmd.length() == 0) return;
  if (cmd == ultimoComandoEjecutado) return;

  Serial.print("Comando remoto: ");
  Serial.println(cmd);

  if (cmd == "UNLOCK") {
    bloqueado        = false;
    intentosFallidos = 0;
    captchaPendiente = false;
    captchaOk        = false;
    apagarTodosLeds();
    registrarEvento(EVT_DESBLOQUEADO);
    Serial.println("Sistema DESBLOQUEADO remotamente");

  } else if (cmd == "LOCK") {
    bloqueado = true;
    apagarTodosLeds();
    digitalWrite(LED_AMARILLO, HIGH);
    registrarEvento(EVT_BLOQUEADO);
    Serial.println("Sistema BLOQUEADO remotamente");

  } else if (cmd == "RESET") {
    bloqueado        = false;
    intentosFallidos = 0;
    captchaPendiente = false;
    captchaOk        = false;
    modoAdmin        = false;
    contrasenaActiva = bitsAEntero(PWD_BITS);
    limiteIntentos   = 3;
    apagarTodosLeds();
    registrarEvento(EVT_RESET);
    Serial.println("Sistema RESETEADO a valores de fabrica");
    Serial.print("Contrasena restaurada: "); imprimirContrasena();

  } else if (cmd.startsWith("L:")) {
    int nuevoLimite = cmd.substring(2).toInt();
    if (nuevoLimite > 0 && nuevoLimite <= 10) {
      limiteIntentos = nuevoLimite;
      registrarEvento(EVT_LIMITE_CAMBIADO);
      Serial.print("Nuevo limite: ");
      Serial.println(limiteIntentos);
    } else {
      Serial.println("Limite invalido — rango: 1 a 10");
    }

  } else if (cmd.startsWith("PWD:")) {
    int nuevaPwd = cmd.substring(4).toInt();
    if (nuevaPwd >= 0 && nuevaPwd <= 1023) {
      contrasenaActiva = nuevaPwd;
      registrarEvento(EVT_PWD_CAMBIADA);
      Serial.print("Nueva contrasena: "); imprimirContrasena();
    } else {
      Serial.println("Contrasena invalida — rango: 0 a 1023");
    }

  } else {
    Serial.print("Comando no reconocido: ");
    Serial.println(cmd);
  }

  ultimoComandoEjecutado = cmd;
}

// ─────────────────────────────────────────────────────────────────
//  Utilidades LED
// ─────────────────────────────────────────────────────────────────
void apagarTodosLeds() {
  digitalWrite(LED_VERDE,    LOW);
  digitalWrite(LED_ROJO,     LOW);
  digitalWrite(LED_AMARILLO, LOW);
}

void parpadear(int pin, int veces, int ms) {
  for (int i = 0; i < veces; i++) {
    digitalWrite(pin, HIGH); delay(ms);
    digitalWrite(pin, LOW);  delay(ms);
  }
}