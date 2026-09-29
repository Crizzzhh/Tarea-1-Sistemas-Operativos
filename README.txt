#Tarea 1: Planificador Dieciochero-Sistemas Operativos

Integrantes: Cristian Hernandez, Diego Soto

## 1. Descripción General
Este proyecto implementa el "Planificador Dieciochero", un simulador que organiza actividades de Fiestas Patrias de forma estructurada. El programa lee un archivo de texto plano (`plan.txt`), modela matemáticamente los días de celebración como un Grafo Acíclico Dirigido (DAG), y orquesta la ejecución de las tareas respetando sus dependencias y un límite máximo de concurrencia.

## 2. Compilación y Ejecución (Modo de Uso)
El código está escrito en C++ y debe compilarse utilizando los flags estrictos exigidos.

**Para compilar:**
g++ -Wall -Wextra -std=c++17 src/planificador.cpp -o planificador -lpthread

*(Nota: El flag -lpthread se incluye por exigencia del formato de compilación de la rúbrica, aunque el uso de hilos está estrictamente prohibido en el diseño).*

**Para ejecutar:**
./planificador plan.txt K

* `plan.txt`: Archivo que contiene la lista de actividades a planificar.
* `K`: Límite máximo de procesos (actividades) ejecutándose de forma concurrente.

## 3. Funciones Implementadas
* **Parseo de datos:** Lectura del archivo línea por línea validando el formato `ID_Actividad: Nombre_Actividad: tiempo_ms: [Dependencias]`. Si una actividad no indica su duración temporal, el sistema le asigna un tiempo aleatorio en un rango entre 100 y 5000 milisegundos.
* **Modelado del DAG:** Construcción de un Grafo Acíclico Dirigido donde cada nodo es una actividad y las aristas representan las dependencias previas necesarias.
* **Creación de procesos:** Se generan procesos independientes para la simulación de cada actividad, respetando la regla estricta de no utilizar hilos.
* **Control de concurrencia (K):** El sistema administra los recursos para que nunca existan más de `K` procesos creados y activos simultáneamente. Las tareas restantes esperan en una cola hasta que se liberen recursos, logrando esto sin esperas activas (busy-waiting) ni condiciones de carrera.
* **Paso de mensajes:** Al finalizar su ejecución, cada actividad propaga un mensaje de texto acotado hacia sus actividades dependientes mediante el uso de tuberías (pipes) para notificar su insumo.
* **Aislamiento de errores:** El simulador está protegido contra fallos internos. Si una actividad falla, el programa no se cierra abruptamente; únicamente se encarga de abortar la rama específica del plan que dependía de esa actividad fallida.
* **Manejo de señales (Seremi):** Se captura la señal `SIGINT` invocada mediante `Ctrl+C`. Esto simula la llegada de la autoridad, provocando que el planificador aborte inmediatamente todas las actividades.

## 4. Justificación de Decisiones de Diseño
* **Sincronización basada en Procesos (No Hilos):** Dado que la rúbrica penaliza el uso de threads (hilos) o sus mecanismos de sincronización con la nota mínima (1.0), toda la arquitectura concurrente se construyó utilizando procesos pesados mediante la llamada al sistema `fork()`. 
* **Prevención de Busy-Waiting con I/O Multiplexing:** Para el paso de mensajes y el control de los procesos activos, se evitó el uso de bucles infinitos. En su lugar, se implementaron llamadas al sistema que bloquean el proceso padre sin consumir CPU hasta que un pipe reciba un mensaje o un proceso hijo envíe un cambio de estado.
* **Gestión algorítmica del DAG:** Se utilizó una estructura que contabiliza las dependencias pendientes de cada nodo. Cuando un proceso hijo termina con éxito y avisa por el pipe, el planificador reduce el contador y, si llega a 0, inserta la tarea en estado "Lista".
* **Control de Concurrencia Centralizado:** Se utilizó un despachador central que evalúa continuamente que haya tareas listas y que la cantidad de procesos activos sea menor estricto que `K`. Esto garantiza el estricto cumplimiento del límite de concurrencia sin condiciones de carrera.
