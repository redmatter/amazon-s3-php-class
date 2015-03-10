
CXXFLAGS += -g -I.

#all: decoder

all: decoder libmimedecoder.a

libmimedecoder.a: ${LIB_OBJECTS}
	ar rcs $@ ${LIB_OBJECTS}

LIB_OBJECTS=decode.o parser.o multipart.o header.o

decoder: ${LIB_OBJECTS} simple.o
	${CXX} ${CXXFLAGS} ${LIB_OBJECTS} simple.o -o $@

mime_decode.o decoder.o: mime/decode.h

mime_decode.o: mime/field.h

test: all
	./decoder < EXAMPLE

test2: all
	./decoder < EXAMPLE2

depend:
	makedepend -Y. -f Makefile *.C

doxy:
	doxygen doxy.config

# DO NOT DELETE

decode.o: ./mime/decode.h ./mime/field.h ./mime/object.h ./mime/parser.h
decode.o: ./mime/client.h ./mime/exception.h ./mime/parser_implementations.h
header.o: ./mime/parser.h ./mime/client.h ./mime/object.h ./mime/field.h
header.o: ./mime/parser_implementations.h ./mime/exception.h
multipart.o: ./mime/parser.h ./mime/client.h ./mime/object.h ./mime/field.h
multipart.o: ./mime/parser_implementations.h ./mime/exception.h
parser.o: ./mime/parser.h ./mime/client.h ./mime/object.h ./mime/field.h
parser.o: ./mime/parser_implementations.h ./mime/exception.h
simple.o: ./mime/decode.h ./mime/field.h ./mime/object.h ./mime/parser.h
simple.o: ./mime/client.h ./mime/exception.h ./mime/parser_implementations.h
