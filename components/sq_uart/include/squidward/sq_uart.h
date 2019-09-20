#ifndef SQ_UART_H
#define SQ_UART_H

extern const char *TAG;

void sq_uart_init();
void sq_uart_send(const char *, size_t);

#endif
