# 
# VERSION CHANGES
#

#BV=$(shell (git rev-list HEAD --count))
#BD=$(shell (date))
BV=1234
BD=today
SDLFLAGS=$(shell (sdl2-config --static-libs --cflags))
CFLAGS=  -O0 -g -ggdb -DBUILD_VER="$(BV)" -DBUILD_DATE=\""$(BD)"\" -DFAKE_SERIAL=$(FAKE_SERIAL)
LIBS=-lSDL2_ttf
CC=gcc
GCC=g++

OBJ=mp730424

default: $(OBJ)
	@echo
	@echo

mp730424: mp730424.cpp
	@echo Build Release $(BV)
	@echo Build Date $(BD)
	${GCC} ${CFLAGS} $(COMPONENTS) mp730424.cpp $(SDLFLAGS) $(LIBS) ${OFILES} -o ${OBJ} 

clean:
	rm -v ${OBJ} 
