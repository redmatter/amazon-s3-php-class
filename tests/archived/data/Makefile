SAMPLE_DURATION=5

THIS_MAKEFILE:=$(abspath $(firstword $(MAKEFILE_LIST)))
THIS_DIR:=$(realpath $(dir $(THIS_MAKEFILE)))

CONVERT_TO_ARCHIVE=$(abspath $(THIS_DIR)/../../../cli/convert-to-archive/convert-to-archive)
ARCHIVING_COMMON_DIR=$(abspath $(THIS_DIR)/../../../common)

DAtA_FILES = test-encrypted-compressed-v2.wav.enc \
             test-encrypted-not-compressed-v2.mp3.enc \
             test-encrypted-compressed-v1.wav.enc \
             test-encrypted-not-compressed-v1.mp3.enc \
             sample.wav \
             sample.mp3

ENCRYPTION_KEY='7BCSLuUcDPpd1bKDRX998fF3Nw0xF0kedHsjDRiCZdo='

all : $(DAtA_FILES)

$(CONVERT_TO_ARCHIVE):
	$(MAKE) -C $(ARCHIVING_COMMON_DIR)
	$(MAKE) -C $(dir $(CONVERT_TO_ARCHIVE))

sample.wav:
	ffmpeg -v warning -f lavfi -i "sine=frequency=1000:duration=$(SAMPLE_DURATION)" -ac 2 sample.wav

sample.mp3:
	ffmpeg -v warning -i sample.wav -acodec mp3 sample.mp3

%-v2.wav.enc: sample.wav $(CONVERT_TO_ARCHIVE)
	$(CONVERT_TO_ARCHIVE) \
		--encryption-key "$(ENCRYPTION_KEY)" \
		--content-type wav \
		--input sample.wav \
		--output $@

%-v2.mp3.enc: sample.mp3 $(CONVERT_TO_ARCHIVE)
	$(CONVERT_TO_ARCHIVE) \
		--encryption-key "$(ENCRYPTION_KEY)" \
		--content-type mp3 \
		--input sample.mp3 \
		--output $@

%-v1.wav.enc: sample.wav $(CONVERT_TO_ARCHIVE)
	$(CONVERT_TO_ARCHIVE) \
		--v1 \
		--encryption-key "$(ENCRYPTION_KEY)" \
		--content-type wav \
		--input sample.wav \
		--output $@

%-v1.mp3.enc: sample.mp3 $(CONVERT_TO_ARCHIVE)
	$(CONVERT_TO_ARCHIVE) \
		--v1 \
		--encryption-key "$(ENCRYPTION_KEY)" \
		--content-type mp3 \
		--input sample.mp3 \
		--output $@

clean:
	rm -f *.enc *.decoded

.PHONY: all clean
