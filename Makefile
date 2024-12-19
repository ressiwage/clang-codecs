usage:
	echo "make fetch_small_bunny_video && make run_hello"

all: clean fetch_bbb_video make_hello run_hello make_remuxing run_remuxing_ts run_remuxing_fragmented_mp4 make_transcoding
.PHONY: all

clean:
	@rm -rf ./build/*

fetch_small_bunny_video:
	./fetch_bbb_video.sh

make_hello: clean
	gcc -g -L/opt/ffmpeg/lib -I/opt/ffmpeg/include/ 0_hello_world.c \
		-lavcodec -lavformat -lavfilter -lavdevice -lswresample -lswscale -lavutil \
		-o ./build/hello

run_hello: make_hello
	./build/hello /home/ressiwage/projects/frames-decoding/b264t.mkv 

run_hello_short: make_hello
	./build/hello /home/ressiwage/projects/test-libav/test-decoding/small_bunny_1080p_60fps.mp4


T_T_S = /home/ressiwage/projects/test-libav/test-decoding/small-bunny-lowres.mp4
T_T_S = /home/ressiwage/projects/test-libav/test-decoding/small_bunny_1080p_60fps.mp4
T_T_S = /home/ressiwage/projects/frames-decoding/b264t.mkv 
T_T_S = /home/ressiwage/projects/frames-decoding/b264t_half.mp4

run_test: make_hello
	cmdbench --iterations 2 --print-averages --print-values --save-json bench.json --save-plot=plot.png "./build/hello $(T_T_S)" && cd temp && python3 ../pgms_to_pngs.py && cd ..

run_test_short: make_hello
	cmdbench --iterations 2 --print-averages --print-values --save-json bench.json --save-plot=plot.png "./build/hello  $(T_T_S)" && \
	cd temp && python3 ../pgms_to_pngs.py && cd .. 

trace_test_short: make_hello
	ltrace -c -S ./build/hello  $(T_T_S)

pgm_to_images:
	cd temp && python3 ../pgms_to_pngs.py && cd ..

images_to_video:
	/bin/python3 /home/ressiwage/projects/test-libav/test-decoding/pngs_to_video.py

clear_temp:
	rm temp/frame-*

trace_bench: run_test_short trace_test_short

build_macro: clean
	gcc -g -L/opt/ffmpeg/lib -I/opt/ffmpeg/include/ 1_extract_macroblocks.c \
		-lavcodec -lavformat -lavfilter -lavdevice -lswresample -lswscale -lavutil \
		-o ./build/macro

run_macro: build_macro
	./build/macro $(T_T_S)

bench_macro: build_macro
	cmdbench "./build/macro $(T_T_S)"

trace_macro: build_macro
	ltrace -c -S ./build/macro $(T_T_S)

build_me: clean
	gcc -g -L/opt/ffmpeg/lib -I/home/ressiwage/projects/FFmpeg-nvidia-build/ffmpeg  4_maxim_example.c -lavcodec -lavformat -lavutil -o build/extract_dc   

run_me: build_me
	./build/extract_dc $(T_T_S)