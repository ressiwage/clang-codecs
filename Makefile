d_ffmpeg = /home/ressiwage/default-ffmpeg/usr/bin/ffmpeg
d_ffmpeg = ffmpeg

usage:
	echo "make fetch_small_bunny_video && make run_hello"

all: clean fetch_bbb_video make_hello run_hello make_remuxing run_remuxing_ts run_remuxing_fragmented_mp4 make_transcoding
.PHONY: all

clean:
	@rm -rf ./build/*
clear_temp:
	rm temp/frame-* ; rm temp/py*.png ; rm temp/dc_frame*.png ; rm temp/dc_*.pgm

fetch_small_bunny_video:
	./fetch_bbb_video.sh

make_hello: clean 
	gcc -g -L/usr/local/lib -I/home/ressiwage/projects/FFmpeg-nvidia-build/ffmpeg 0_hello_world.c \
		-lavcodec -lavformat -lavfilter -lavdevice -lswresample -lswscale -lavutil \
		-o ./build/hello

run_hello: make_hello 
	clear_temp;./build/hello /home/ressiwage/projects/frames-decoding/b264t_half.mp4 

run_hello_short: make_hello
	./build/hello /home/ressiwage/projects/test-libav/test-decoding/small_bunny_1080p_60fps.mp4



T_T_S = /home/ressiwage/projects/test-libav/test-decoding/small-bunny-lowres.mp4
# T_T_S = /home/ressiwage/projects/test-libav/test-decoding/small_bunny_1080p_60fps.mp4
#  T_T_S = /home/ressiwage/projects/frames-decoding/b264t.mkv
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



trace_bench: run_test_short trace_test_short

build_macro: clean
	gcc -g -L/usr/local/lib -I/home/ressiwage/projects/FFmpeg-nvidia-build/ffmpeg  1_extract_macroblocks.c \
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

build_me_f: clean 
	make clear_temp; gcc -g -L/usr/local/lib -I/home/ressiwage/projects/FFmpeg-nvidia-build/ffmpeg  5_maxim_ex_fixed.c -lavcodec -lavformat -lavutil -lavcodec -lavformat -lavfilter -lavdevice -lswresample -lswscale -lavutil -o build/extract_dc_f

run_me_f: build_me_f
	./build/extract_dc_f $(T_T_S)

pipeline_f: run_me_f 
	mv output_dc.mp4 output_dc_prev.mp4; ffmpeg -y -framerate 30 -i temp/dc_frame_%d.pgm -vf "pad=width=ceil(iw/2)*2:height=ceil(ih/2)*2" -c:v libx264 -pix_fmt yuv420p output_dc.mp4
	make clear_temp