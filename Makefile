NAME= ipcbuf

CFLAGS= -Wall -Werror -Wextra

PREFIX?=/usr/local

all: ${NAME}

help:
	@echo "The following targets are available:"
	@echo "${NAME}  build the ${NAME} executable"
	@echo "clean    remove executable and intermediate files"
	@echo "install  install ${NAME} under ${PREFIX}"

# OmniOS needs -lsocket; I should add some OS-specific
# logic here to set the LDFLAGS.
${NAME}: ${NAME}.c
	${CC} ${CFLAGS} -o ${NAME} ${NAME}.c

clean:
	rm -fr ${NAME}

install: ${NAME}
	mkdir -p ${PREFIX}/bin ${PREFIX}/share/man/man1
	install -c -m 555 ${NAME} ${PREFIX}/bin/${NAME}
	install -c -m 444 ${NAME}.1 ${PREFIX}/share/man/man1/${NAME}.1
