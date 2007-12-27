#
# Nagra
#
TARGET = nagra
OBJS   = nagra.o nagra1.o nagra2.o cpu.o
LIBS   = -lcrypto
CLEAN_RM = nagra2-prov.c

nagra2-prov.c:
	echo >$@ "/* generated file, do not edit */"
	find -name "nagra2-[0-9][0-9][0-9][0-9].c" -printf '#include "%f"\n' >>$@
	@rm .dependencies
