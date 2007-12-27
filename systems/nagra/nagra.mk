#
# Nagra
#
TARGET = nagra
OBJS   = nagra.o nagra1.o nagra2.o cpu.o \
         $(patsubst %.c,%.o,$(wildcard nagra2-[0-9][0-9][0-9][0-9].c))
LIBS   = -lcrypto
