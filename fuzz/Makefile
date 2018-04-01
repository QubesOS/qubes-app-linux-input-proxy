
CFLAGS += -I../src

all: input-proxy-receiver_fuzzer input-proxy-receiver_fuzzer_seed_corpus.zip

input-proxy-receiver_fuzzer_seed_corpus.zip: seed-corpus-in/*.bin
	rm -rf seed-corpus
	mkdir -p seed-corpus
	for descr in seed-corpus-in/descriptor*.bin; do \
		for events in seed-corpus-in/events*.bin; do \
			cat $$descr $$events > seed-corpus/$$(basename $$descr)-$$(basename $$events); \
		done; \
	done
	zip -q -r $@ seed-corpus

input-proxy-receiver_fuzzer: common.o input-proxy-receiver.o input-proxy-receiver_fuzzer.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIB_FUZZING_ENGINE)

common.o: ../src/common.c
	$(CC) $(CFLAGS) -o $@ -c $^
input-proxy-receiver.o: ../src/input-proxy-receiver.c
	$(CC) $(CFLAGS) -o $@ -c $^
