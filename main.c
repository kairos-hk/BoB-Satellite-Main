#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <string.h>

#define NRF24L01_CE PB1
#define NRF24L01_CSN PB2
#define NRF_MISO PB4
#define NRF_MOSI PB3
#define NRF_SCK PB5

#define BME280_SDA PC4
#define BME280_SCL PC5
#define BME280_ADDRESS 0x76

#define R_RX_PAYLOAD 0x61
#define W_TX_PAYLOAD 0xA0

uint16_t dig_T1;
int16_t dig_T2, dig_T3;

void USART_init(unsigned int baud) {
	unsigned int ubrr = F_CPU/16/baud-1;
	UBRR0H = (unsigned char)(ubrr>>8);
	UBRR0L = (unsigned char)ubrr;
	UCSR0B = (1<<RXEN0)|(1<<TXEN0);
	UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);
}

void USART_transmit(char data) {
	while (!(UCSR0A & (1<<UDRE0)));
	UDR0 = data;
}

char USART_receive(void) {
	while (!(UCSR0A & (1<<RXC0)));
	return UDR0;
}

void USART_send_string(const char* str) {
	while (*str) {
		USART_transmit(*str++);
	}
}

void SPI_init() {
	DDRB |= (1<<NRF_MOSI)|(1<<NRF_SCK)|(1<<NRF24L01_CE)|(1<<NRF24L01_CSN);
	DDRB &= ~(1<<NRF_MISO);
	SPCR = (1<<SPE)|(1<<MSTR)|(1<<SPR0);
	PORTB &= ~(1<<NRF24L01_CE);
	PORTB |= (1<<NRF24L01_CSN);
}

uint8_t SPI_transfer(uint8_t data) {
	SPDR = data;
	while(!(SPSR & (1<<SPIF)));
	return SPDR;
}

void nrf24_init() {
	SPI_init();
	PORTB &= ~(1<<NRF24L01_CSN);
	SPI_transfer(0x20);
	SPI_transfer(0x0E);
	PORTB |= (1<<NRF24L01_CSN);
}

void TWI_init() {
	TWSR = 0x00;
	TWBR = ((F_CPU/100000UL)-16)/2;
}

void TWI_start() {
	TWCR = (1<<TWSTA)|(1<<TWEN)|(1<<TWINT);
	while (!(TWCR & (1<<TWINT)));
}

void TWI_stop() {
	TWCR = (1<<TWSTO)|(1<<TWEN)|(1<<TWINT);
}

void TWI_write(uint8_t data) {
	TWDR = data;
	TWCR = (1<<TWINT)|(1<<TWEN);
	while (!(TWCR & (1<<TWINT)));
}

uint8_t TWI_read_ack() {
	TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWEA);
	while (!(TWCR & (1<<TWINT)));
	return TWDR;
}

uint8_t TWI_read_nack() {
	TWCR = (1<<TWINT)|(1<<TWEN);
	while (!(TWCR & (1<<TWINT)));
	return TWDR;
}

void bme280_init() {
	TWI_start();
	TWI_write((BME280_ADDRESS << 1) | 0);
	TWI_write(0x88);
	TWI_stop();

	TWI_start();
	TWI_write((BME280_ADDRESS << 1) | 1);
	dig_T1 = TWI_read_ack() | (TWI_read_ack() << 8);
	dig_T2 = TWI_read_ack() | (TWI_read_ack() << 8);
	dig_T3 = TWI_read_nack() | (TWI_read_nack() << 8);
	TWI_stop();
}

float bme280_read_temperature() {
	int32_t var1, var2, t_fine;
	TWI_start();
	TWI_write((BME280_ADDRESS << 1) | 0);
	TWI_write(0xF7);
	TWI_stop();

	TWI_start();
	TWI_write((BME280_ADDRESS << 1) | 1);
	int32_t raw_temp = (TWI_read_ack() << 12) | (TWI_read_ack() << 4) | (TWI_read_nack() >> 4);
	TWI_stop();

	var1 = ((((raw_temp >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
	var2 = (((((raw_temp >> 4) - ((int32_t)dig_T1)) * ((raw_temp >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
	t_fine = var1 + var2;
	float temperature = (t_fine * 5 + 128) >> 8;
	return temperature / 100.0;
}

int main() {
	USART_init(9600);
	nrf24_init();
	bme280_init();

	while (1) {
		PORTB |= (1<<NRF24L01_CE);
		_delay_ms(10);

		PORTB &= ~(1<<NRF24L01_CSN);
		SPI_transfer(R_RX_PAYLOAD);
		char encryptedMessage[32] = "";
		for (int i = 0; i < 32; i++) {
			encryptedMessage[i] = SPI_transfer(0xFF);
		}
		PORTB |= (1<<NRF24L01_CSN);

		USART_send_string(encryptedMessage);
		USART_send_string("\r\n");

		char decryptedMessage[32];
		int i = 0;
		while (1) {
			decryptedMessage[i] = USART_receive();
			if (decryptedMessage[i] == '\n') {
				decryptedMessage[i] = '\0';
				break;
			}
			i++;
		}

		if (strcmp(decryptedMessage, "hello") == 0) {
			USART_send_string("hello\r\n");

			float temperature = bme280_read_temperature();
			char tempMessage[32];
			sprintf(tempMessage, "me too, Temp: %.2f", temperature);
			USART_send_string(tempMessage);
			USART_send_string("\r\n");

			char encryptedTempMessage[32];
			i = 0;
			while (1) {
				encryptedTempMessage[i] = USART_receive();
				if (encryptedTempMessage[i] == '\n') {
					encryptedTempMessage[i] = '\0';
					break;
				}
				i++;
			}

			PORTB &= ~(1<<NRF24L01_CSN);
			SPI_transfer(W_TX_PAYLOAD);
			for (int i = 0; i < 32; i++) {
				SPI_transfer(encryptedTempMessage[i]);
			}
			PORTB |= (1<<NRF24L01_CSN);
			} else {
			USART_send_string("Not hello\r\n");
		}

		_delay_ms(1000);
	}

	return 0;
}
