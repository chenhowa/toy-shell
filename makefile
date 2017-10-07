CC = gcc
CFLAGS = -Wall -pedantic

SRC = smallsh.c
OBJ = smallsh.o
HEADERS = 

smallsh: ${OBJ} ${HEADERS}
	${CC} ${SRC} -o smallsh

${OBJ}: ${SRC}
	${CC} ${CFLAGS} -c $(@:.o=.c)


debug: ${OBJ}
	${CC} ${CFLAGS} -g ${SRC} -o debug

clean: 
	rm -r *.o debug smallsh 
