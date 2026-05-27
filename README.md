# Sistema de control de acceso IoT

Firmware para ESP32 con lector de DIP switches, control por botones, captcha físico con potenciómetro y sincronización con ThingSpeak.

## Funciones

- Activación y desactivación del sistema con botón dedicado.
- Verificación de contraseña física mediante DIP switches.
- Bloqueo por intentos fallidos con desbloqueo remoto.
- Modo administrador para pruebas sin penalización.
- Reporte de eventos y estado en ThingSpeak.

## Requisitos

- PlatformIO
- ESP32 Dev Module
- Librería `ThingSpeak`

## Configuración

1. Copia `include/secrets.h.example` como `include/secrets.h`.
2. Reemplaza los valores por tu red WiFi y tus llaves de ThingSpeak.
3. Compila y sube el proyecto desde PlatformIO.

## Estructura

- `src/main.cpp`: lógica principal del sistema.
- `include/secrets.h.example`: plantilla para credenciales locales.
- `platformio.ini`: configuración de compilación.

## Seguridad

No se incluyen credenciales reales en el repositorio. Cada instalación debe usar su propio archivo `include/secrets.h`.