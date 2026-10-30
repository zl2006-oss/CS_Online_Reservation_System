all : server stress

server : server.cpp 
	g++ -o server server.cpp -lpthread -ljsoncpp -lmysqlclient -lpthread -g

stress : stress_client.cpp
	g++ -o stress stress_client.cpp -g

clean:
	rm  -f *.o server stress

