CFLAGS := -O2 -Wall

attack: attack.cc
	$(CXX) $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm -f attack
