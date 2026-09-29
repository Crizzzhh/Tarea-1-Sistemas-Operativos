CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
LDLIBS = -lpthread

planificador: src/planificador.cpp
	$(CXX) $(CXXFLAGS) -o planificador src/planificador.cpp $(LDLIBS)

clean:
	rm -f planificador
