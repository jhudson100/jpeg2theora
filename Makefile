
all: theora2jpeg jpeg2theora

theora2jpeg: theora2jpeg.cpp
	g++ -g theora2jpeg.cpp -o theora2jpeg -logg  -ltheora -ltheoraenc -ltheoradec -lturbojpeg
	
jpeg2theora: jpeg2theora.cpp
	g++ -g jpeg2theora.cpp -o jpeg2theora -logg  -ltheora -ltheoraenc -ltheoradec -lturbojpeg
