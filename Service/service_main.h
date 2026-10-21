#ifndef SERVICE_MAIN_H
#define SERVICE_MAIN_H
/* BSP must be ready. Call once before creating tasks, with IRQs enabled. */
void ServiceMain_Init(void);
/* One Service task owns input and motion. Sensor has its own task. */
void ServiceMain_Process(void);
#endif
