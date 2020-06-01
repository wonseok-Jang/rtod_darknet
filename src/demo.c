#include "network.h"
#include "detection_layer.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "image.h"
#include "image_opencv.h"
#include "demo.h"
#include "darknet.h"
#include "v4l2.h"
#ifdef WIN32
#include <time.h>
#include "gettimeofday.h"
#else
#include <sys/time.h>
#endif

#define iteration 1000
#define start_log 25
#define cycle 1

/* CPU & GPU information */
#define NUM_TRACE 4 /* GPU power, CPU power, GPU temperature, CPU temperature */
#define GPU_POWER_PATH "/sys/bus/i2c/devices/1-0040/iio_device/in_power0_input"
#define CPU_POWER_PATH "/sys/bus/i2c/devices/1-0040/iio_device/in_power1_input"
#define GPU_TEMP_PATH "/sys/devices/virtual/thermal/thermal_zone1/temp"
#define CPU_TEMP_PATH "/sys/devices/virtual/thermal/thermal_zone0/temp"

#ifdef TRACE

/* CPU & GPU information log */
#define TRACE_PATH "trace"
#define TRACE_FILE "/trace.csv"

#endif

/* architecture */
#define PARALLEL
//#define SEQUENTIAL
//#define CONTENTION_FREE 

/* Measurement */
#define MEASUREMENT_PATH "measure"
#define MEASUREMENT_FILE "/measure.csv"

/* calculate inter frame gap */
#define GET_IFG(x,y) ((x) - (y)); \
                     (y) = (x);

#define MAX(x,y) (((x) < (y) ? (y) : (x)))

#ifdef V4L2

#include "v4l2.h"

#endif

#ifdef OPENCV

#include "http_stream.h"

struct frame_data frame[3]; // v4l2 image data

static double image_waiting_array[iteration];
static double fetch_array[iteration];
static double detect_array[iteration];
static double display_array[iteration];
static double slack[iteration];
static double fps_array[iteration];
static double latency[iteration];
static double select_array[iteration];
static int inter_frame_gap_array[iteration];

double frame_timestamp[3];
static int buff_index=0;
int sleep_time;
int cnt=0;
int display_index;
int detect_index;
int fetch_offset = 0; // zero slack

int frame_sequence_tmp;
int inter_frame_gap;

#ifdef TRACE
/* Check first iteration */
int first_trace;

/* Trace iteration */
int trace_iter = 1;

static double trace_array[NUM_TRACE][iteration];
#endif

pthread_mutex_t mutex_lock;
int lock_offset = 5; // ms
static double detect_start;
static double detect_end;
static double detect_time;
static double display_time;
static double display_end;
static double fetch_start;
static double fetch_time;
static double image_waiting_time;
double select_time;

static char **demo_names;
static image **demo_alphabet;
static int demo_classes;

static int nboxes = 0;
static detection *dets = NULL;

static network net;
static image in_s ;
static image det_s;

static cap_cv *cap;
static float fps = 0;
static float demo_thresh = 0;
static int demo_ext_output = 0;
static long long int frame_id = 0;
static int demo_json_port = -1;

#define NFRAMES 3

static float* predictions[NFRAMES];
static int demo_index = 0;
static mat_cv* cv_images[NFRAMES];
static float *avg;

mat_cv* in_img;
mat_cv* det_img;
mat_cv* show_img;

static volatile int flag_exit;
static int letter_box = 0;

double gettimeafterboot()
{
	struct timespec time_after_boot;
	clock_gettime(CLOCK_MONOTONIC,&time_after_boot);
	return (time_after_boot.tv_sec*1000+time_after_boot.tv_nsec*0.000001);
}

void *fetch_in_thread(void *ptr)
{

	/* Zero slack */

	//printf("Fetch offset : %d\n", fetch_offset);
	usleep(fetch_offset * 1000);

#ifdef CONTENTION_FREE
	usleep(lock_offset * 1000);
	pthread_mutex_lock(&mutex_lock);
#endif

	fetch_start = gettimeafterboot();

    int dont_close_stream = 0;    // set 1 if your IP-camera periodically turns off and turns on video-stream
    if(letter_box)
        in_s = get_image_from_stream_letterbox(cap, net.w, net.h, net.c, &in_img, dont_close_stream);
    else{
#ifdef V4L2
		frame[buff_index].frame = capture_image(&frame[buff_index]);
		//printf("fetch w, h : %d, %d\n", frame[buff_index].frame.w, frame[buff_index].frame.h);
		letterbox_image_into(frame[buff_index].frame, net.w, net.h, frame[buff_index].resize_frame);
		//frame[buff_index].resize_frame = letterbox_image(frame[buff_index].frame, net.w, net.h);
		//show_image_cv(frame[buff_index].resize_frame,"im");

		if(!frame[buff_index].resize_frame.data){
			printf("Stream closed.\n");
			flag_exit = 1;
			//exit(EXIT_FAILURE);
			return 0;
		}
#else
        in_s = get_image_from_stream_resize_with_timestamp(cap, net.w, net.h, net.c, &in_img, dont_close_stream, &frame[buff_index]);
//        in_s = get_image_from_v4l2(net.w, net.h, net.c, &in_img, dont_close_stream, &frame[buff_index]);
		if(!in_s.data){
			printf("Stream closed.\n");
			flag_exit = 1;
			//exit(EXIT_FAILURE);
			return 0;
		}
#endif
	}

#ifdef CONTENTION_FREE
	pthread_mutex_unlock(&mutex_lock);
#endif

	image_waiting_time = frame[buff_index].frame_timestamp-fetch_start;
	inter_frame_gap = GET_IFG(frame[buff_index].frame_sequence, frame_sequence_tmp);

    //in_s = resize_image(in, net.w, net.h);

	//printf("Image time stamp : %f\n", frame_timestamp[buff_index]);

	fetch_time = gettimeafterboot() - fetch_start;

	if(cnt >= start_log){
		fetch_array[cnt-start_log]=fetch_time;
		image_waiting_array[cnt-start_log]=image_waiting_time;
		select_array[cnt-start_log] = select_time;
		inter_frame_gap_array[cnt-start_log] = inter_frame_gap;
	}

	printf("fetch_time : %f\n", fetch_time);
	printf("image waiting time : %f\n", image_waiting_time);

    return 0;
}

void *detect_in_thread(void *ptr)
{
#ifdef CONTENTION_FREE
	pthread_mutex_lock(&mutex_lock);
#endif

   // show_image_cv(det_img, "test");
	detect_start = gettimeafterboot();
	
    layer l = net.layers[net.n-1];
#ifdef V4L2
    float *X = frame[detect_index].resize_frame.data;
#else
    float *X = det_s.data;
#endif
    float *prediction = network_predict(net, X);

    memcpy(predictions[demo_index], prediction, l.outputs*sizeof(float));
    mean_arrays(predictions, NFRAMES, l.outputs, avg);
    l.output = avg;

#ifndef V4L2
	cv_images[demo_index] = det_img;
    det_img = cv_images[(demo_index + NFRAMES / 2 + 1) % NFRAMES];
#endif
    demo_index = (demo_index + 1) % NFRAMES;

#ifdef V4L2
        dets = get_network_boxes(&net, net.w, net.h, demo_thresh, demo_thresh, 0, 1, &nboxes, 0); // resized
#else
    if (letter_box)
        dets = get_network_boxes(&net, get_width_mat(in_img), get_height_mat(in_img), demo_thresh, demo_thresh, 0, 1, &nboxes, 1); // letter box
    else
        dets = get_network_boxes(&net, net.w, net.h, demo_thresh, demo_thresh, 0, 1, &nboxes, 0); // resized
#endif

#ifdef CONTENTION_FREE
	pthread_mutex_unlock(&mutex_lock);
#endif

	detect_time = gettimeafterboot() - detect_start;

	if(cnt >= start_log) detect_array[cnt-start_log]=detect_time;

	printf("Detect time : %f\n", detect_time);
    return 0;
}

#ifdef V4L2
void *display_in_thread(void *ptr)
{
    int c = show_image_cv(frame[display_index].frame, "Demo");
	
	if (c == 27 || c == 1048603) // ESC - exit (OpenCV 2.x / 3.x)
	{
		flag_exit = 1;
	}

	return 0;
}
#endif

#ifdef TRACE
void *read_data()
{
	static int trace_sleep = 0;
	//FILE *fp_cpu, *fp_gpu;
	FILE *fp[NUM_TRACE];

	int cpu_data;
	int gpu_data;
	int data[NUM_TRACE] = {0,}; // GPU power, CPU power, GPU temp, CPU temp
	char path[NUM_TRACE][100] = {GPU_POWER_PATH, CPU_POWER_PATH, GPU_TEMP_PATH, CPU_TEMP_PATH};

	if (!first_trace)
	{
		for (int i = 0; i < trace_iter; i++)
		{
			usleep(trace_sleep * 1000);

			for(int k = 0; k < NUM_TRACE; k++)
			{
				fp[k] = fopen(path[k], "r");

				if(fp[k] == NULL) printf("File open fail\n");

				fscanf(fp[k], "%d", data + k);

				//		printf("data : %d\n", data[k]);

				fclose(fp[k]);
			}

			if (cnt >= start_log){
				for (int j = 0; j < NUM_TRACE; j++) 
					trace_array[j][(cnt - start_log) + i] = data[j];
				//trace_array[j][(count - start_log)] = data[j];
			}
		}
	}
	else 
	{
		trace_sleep = (int)(1000. / fps) / 2.; 
		printf("trace_sleep : %f\n", trace_sleep);
		first_trace = 0;
	}
	return 0;
}
#endif

double get_wall_time()
{
    struct timeval walltime;
    if (gettimeofday(&walltime, NULL)) {
        return 0;
    }
    return (double)walltime.tv_sec + (double)walltime.tv_usec * .000001;
}

void demo(char *cfgfile, char *weightfile, float thresh, float hier_thresh, int cam_index, const char *filename, char **names, int classes,
    int frame_skip, char *prefix, char *out_filename, int mjpeg_port, int json_port, int dont_show, int ext_output, int letter_box_in, int time_limit_sec, char *http_post_host,
    int benchmark, int benchmark_layers, int offset)
{
    letter_box = letter_box_in;
	in_img = det_img = show_img = NULL;
	//skip = frame_skip;
    image **alphabet = load_alphabet();
    int delay = frame_skip;
    demo_names = names;
    demo_alphabet = alphabet;
    demo_classes = classes;
    demo_thresh = thresh;
    demo_ext_output = ext_output;
    demo_json_port = json_port;
    printf("Demo\n");
    net = parse_network_cfg_custom(cfgfile, 1, 1);    // set batch=1
    if(weightfile){
        load_weights(&net, weightfile);
    }
    net.benchmark_layers = benchmark_layers;
    fuse_conv_batchnorm(net);
    calculate_binary_weights(net);
    srand(2222222);

	//printf("buffer_size : %d\n", opencv_buffer_size);
	printf("offset : %d\n", offset);
	fetch_offset = offset;

    if(filename){
        printf("video file: %s\n", filename);
        cap = get_capture_video_stream(filename);
    }else{
        printf("Webcam index: %d\n", cam_index);
#ifdef V4L2
		char cam_dev[20] = "/dev/video";
		char index[2];
		sprintf(index, "%d", cam_index);
		strcat(cam_dev, index);
		printf("cam dev : %s\n", cam_dev);

		int frames = 30;
		int w = 640;
		int h = 480;
		if(open_device(cam_dev, frames, w, h) < 0)
		{
			error("Couldn't connect to webcam.\n");

		}
#else
        cap = get_capture_webcam(cam_index);

		if (!cap) {
#ifdef WIN32
			printf("Check that you have copied file opencv_ffmpeg340_64.dll to the same directory where is darknet.exe \n");
#endif
			error("Couldn't connect to webcam.\n");
		}
#endif
	}

    layer l = net.layers[net.n-1];
    int j;

    avg = (float *) calloc(l.outputs, sizeof(float));
    for(j = 0; j < NFRAMES; ++j) predictions[j] = (float *) calloc(l.outputs, sizeof(float));

    if (l.classes != demo_classes) {
        printf("\n Parameters don't match: in cfg-file classes=%d, in data-file classes=%d \n", l.classes, demo_classes);
        getchar();
        exit(0);
    }

    flag_exit = 0;

    pthread_t fetch_thread;
    pthread_t detect_thread;
#ifdef TRACE
    pthread_t trace_thread;
#endif

#ifdef V4L2
	frame[0].frame = capture_image(&frame[buff_index]);
	frame[0].resize_frame = letterbox_image(frame[0].frame, net.w, net.h);

	frame[1].frame = frame[0].frame;
	frame[1].resize_frame = letterbox_image(frame[0].frame, net.w, net.h);

	frame[2].frame = frame[0].frame;
	frame[2].resize_frame = letterbox_image(frame[0].frame, net.w, net.h);

#else
    fetch_in_thread(0);
    det_img = in_img;
	det_s = in_s;

	fetch_in_thread(0);
    detect_in_thread(0);
    det_img = in_img;
	det_s = in_s;

    for (j = 0; j < NFRAMES / 2; ++j) {
		free_detections(dets, nboxes);
        fetch_in_thread(0);
        detect_in_thread(0);
        det_img = in_img;
        det_s = in_s;
    }
#endif

    int count = 0;
    if(!prefix && !dont_show){
        int full_screen = 0;
        create_window_cv("Demo", full_screen, 1352, 1013);
		//make_window("Demo", 1352, 1013, full_screen);
    }

    write_cv* output_video_writer = NULL;
    if (out_filename && !flag_exit)
    {
        int src_fps = 25;
        src_fps = get_stream_fps_cpp_cv(cap);
#ifndef V4L2
        output_video_writer =
            create_video_writer(out_filename, 'D', 'I', 'V', 'X', src_fps, get_width_mat(det_img), get_height_mat(det_img), 1);
#endif

        //'H', '2', '6', '4'
        //'D', 'I', 'V', 'X'
        //'M', 'J', 'P', 'G'
        //'M', 'P', '4', 'V'
        //'M', 'P', '4', '2'
        //'X', 'V', 'I', 'D'
        //'W', 'M', 'V', '2'
    }

    int send_http_post_once = 0;
    const double start_time_lim = get_time_point();
    double before = get_time_point();
    double before_1 = gettimeafterboot();
    double start_time = get_time_point();
    float avg_fps = 0;
    int frame_counter = 0;

	double image_waiting_sum[cycle]={0};
	double fetch_sum[cycle]={0};
	double detect_sum[cycle]={0};
	double display_sum[cycle]={0};
	double slack_sum[cycle]={0};
	double fps_sum[cycle]={0};
	double latency_sum[cycle]={0};
	double select_sum[cycle]={0};
	double trace_data_sum[NUM_TRACE]={0};
	int inter_frame_gap_sum[cycle]={0};

#ifdef CONTENTION_FREE
	pthread_mutex_init(&mutex_lock, NULL);
#endif

	for(int iter=0;iter<cycle;iter++){
		while(1){
			printf("================start================\n");
			++count;
			{
				/* Image index */
#if (defined PARALLEL)
				display_index = (buff_index + 1) %3;
				detect_index = (buff_index + 2) %3;
#elif (defined SEQUENTIAL)
				display_index = (buff_index) %3;
				detect_index = (buff_index) %3;
#elif (defined CONTENTION_FREE)
				display_index = (buff_index + 2) %3;
				detect_index = (buff_index + 2) %3;
#endif
				const float nms = .45;    // 0.4F
				int local_nboxes = nboxes;
				detection *local_dets = dets;

#ifndef SEQUENTIAL
				/* Parallel or Contention free */
				if (!benchmark) if (pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
#endif

#ifdef TRACE
				/* Trace CPU, GPU thermal, power */
				if(pthread_create(&trace_thread, 0, read_data, 0)) error("Thread creation failed");
#endif

#ifdef PARALLEL 
				/* Sequential or Contention free */
				if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
				//if (nms) do_nms_obj(local_dets, local_nboxes, l.classes, nms);    // bad results
				if (nms) {
					if (l.nms_kind == DEFAULT_NMS) do_nms_sort(local_dets, local_nboxes, l.classes, nms);
					else diounms_sort(local_dets, local_nboxes, l.classes, nms, l.nms_kind, l.beta_nms);
				}
#endif

#ifdef SEQUENTIAL
				/* Sequential */
				fetch_in_thread(0);

				det_img = in_img;
				det_s = in_s;
#endif

#if (defined SEQUENTIAL || defined CONTENTION_FREE)
				/* Sequential or Contention free*/
				detect_in_thread(0);

				if (nms) {
					if (l.nms_kind == DEFAULT_NMS) do_nms_sort(local_dets, local_nboxes, l.classes, nms);
					else diounms_sort(local_dets, local_nboxes, l.classes, nms, l.nms_kind, l.beta_nms);
				}
				show_img = det_img;
#endif

				/* display thread */

				double display_start = gettimeafterboot();
#ifdef V4L2
				//if (!benchmark) draw_detections_v3(display, local_dets, local_nboxes, demo_thresh, demo_names, demo_alphabet, demo_classes, demo_ext_output);
				if (!benchmark) draw_detections_v3(frame[display_index].frame, local_dets, local_nboxes, demo_thresh, demo_names, demo_alphabet, demo_classes, demo_ext_output);
				free_detections(local_dets, local_nboxes);

				/* Image display */
				display_in_thread(0);
#else
				/* original display thread */

				//printf("\033[2J");
				//printf("\033[1;1H");
				//printf("\nFPS:%.1f\n", fps);
				printf("Objects:\n\n");

				++frame_id;
				if (demo_json_port > 0) {
					int timeout = 400000;
					send_json(local_dets, local_nboxes, l.classes, demo_names, frame_id, demo_json_port, timeout);
				}

				//char *http_post_server = "webhook.site/898bbd9b-0ddd-49cf-b81d-1f56be98d870";
				if (http_post_host && !send_http_post_once) {
					int timeout = 3;            // 3 seconds
					int http_post_port = 80;    // 443 https, 80 http
					if (send_http_post_request(http_post_host, http_post_port, filename,
								local_dets, nboxes, classes, names, frame_id, ext_output, timeout))
					{
						if (time_limit_sec > 0) send_http_post_once = 1;
					}
				}

				if (!benchmark) draw_detections_cv_v3(show_img, local_dets, local_nboxes, demo_thresh, demo_names, demo_alphabet, demo_classes, demo_ext_output);
				free_detections(local_dets, local_nboxes);

				printf("\nFPS:%.1f \t AVG_FPS:%.1f\n", fps, avg_fps);

				if(!prefix){
					if (!dont_show) {
						show_image_mat(show_img, "Demo");
						int c = wait_key_cv(1);
						if (c == 10) {
							if (frame_skip == 0) frame_skip = 60;
							else if (frame_skip == 4) frame_skip = 0;
							else if (frame_skip == 60) frame_skip = 4;
							else frame_skip = 0;
						}
						else if (c == 27 || c == 1048603) // ESC - exit (OpenCV 2.x / 3.x)
						{
							flag_exit = 1;
						}
					}
				}else{
					char buff[256];
					sprintf(buff, "%s_%08d.jpg", prefix, count);
					if(show_img) save_cv_jpg(show_img, buff);
				}

				// if you run it with param -mjpeg_port 8090  then open URL in your web-browser: http://localhost:8090
				if (mjpeg_port > 0 && show_img) {
					int port = mjpeg_port;
					int timeout = 400000;
					int jpeg_quality = 40;    // 1 - 100
					send_mjpeg(show_img, port, timeout, jpeg_quality);
				}

				// save video file
				if (output_video_writer && show_img) {
					write_frame_cv(output_video_writer, show_img);
					printf("\n cvWriteFrame \n");
				}
#endif
				/* display end */

				display_end = gettimeafterboot();

				display_time = display_end - display_start; 

				printf("display : %f\n", display_time);

#ifndef CONTENTION_FREE
				/* Prallel or Sequential */
				pthread_join(detect_thread, 0);
#endif

#ifdef TRACE
				/* TRACE end */
				pthread_join(trace_thread, 0);
#endif

#ifndef SEQUENTIAL
				/* Parallel or Contention free */
				if (!benchmark) {
					pthread_join(fetch_thread, 0);
					free_image(det_s);
				}
#endif
				if (time_limit_sec > 0 && (get_time_point() - start_time_lim)/1000000 > time_limit_sec) {
					printf(" start_time_lim = %f, get_time_point() = %f, time spent = %f \n", start_time_lim, get_time_point(), get_time_point() - start_time_lim);
					break;
				}

				if (flag_exit == 1) break;

				if(delay == 0){

#ifndef V4L2
					if(!benchmark) release_mat(&show_img);
#endif

#ifndef CONTENTION_FREE
					/* Parallel or Sequential */
					show_img = det_img;
#endif
				}
				det_img = in_img;
				det_s = in_s;
			}
			--delay;
			if(delay < 0){
				delay = frame_skip;

				//double after = get_wall_time();
				//float curr = 1./(after - before);
				double after = get_time_point();    // more accurate time measurements
				double after_1 = gettimeafterboot();    
				float curr = 1000000. / (after - before);
				float curr_1 = (after_1 - before_1);
				printf("FPS : %f\n",1000.0/curr_1);
				//fps = fps*0.9 + curr*0.1;  
				fps = 1000.0/curr_1;
				before = after;
				before_1 = after_1;

				float spent_time = (get_time_point() - start_time) / 1000000;
				frame_counter++;
				if (spent_time >= 3.0f) {
					//printf(" spent_time = %f \n", spent_time);
					avg_fps = frame_counter / spent_time;
					frame_counter = 0;
					start_time = get_time_point();
				}
			}

			if(cnt>=start_log){
				fps_array[cnt-start_log]=fps;
				latency[cnt-start_log]=display_end-frame[display_index].frame_timestamp;
				display_array[cnt-start_log]=display_time;
#ifdef PARALLEL
				slack[cnt-start_log] = (MAX(MAX(fetch_time, detect_time), display_time))-(sleep_time+fetch_time);
#endif

#ifdef CONTENTION_FREE
				slack[cnt-start_log] = (detect_time + display_time)-(sleep_time+fetch_time);
#endif

#ifdef SEQUENTIAL
				slack[cnt-start_log] = .0;
#endif

				printf("latency: %f\n",latency[cnt-start_log]);
				printf("cnt : %d\n",cnt);
			}

			if(cnt==((iteration+start_log)-1)){
				int exist=0;
				FILE *fp;
				char file_path[100] = "";

				strcat(file_path, MEASUREMENT_PATH);
				strcat(file_path, MEASUREMENT_FILE);

				fp=fopen(file_path,"w+");

				if (fp == NULL) 
				{
					/* make directory */
					while(!exist){
						int result;

						result = mkdir(MEASUREMENT_PATH, 0766);

						if(result == 0) { 
							exist = 1;

							fp=fopen(file_path,"w+");
						}
					}
				}

				for(int i=0;i<iteration;i++){
					image_waiting_sum[iter]+=image_waiting_array[i];
					fetch_sum[iter]+=fetch_array[i];
					detect_sum[iter]+=detect_array[i];
					display_sum[iter]+=display_array[i];
					slack_sum[iter]+=slack[i];
					fps_sum[iter]+=fps_array[i];
					latency_sum[iter]+=latency[i];
					select_sum[iter]+=select_array[i];
					inter_frame_gap_sum[iter]+=inter_frame_gap_array[i];

					fprintf(fp,"%f,%f,%f,%f,%f,%f,%f,%f,%d\n",image_waiting_array[i],fetch_array[i],detect_array[i],	display_array[i],slack[i],fps_array[i],latency[i], select_array[i], inter_frame_gap_array[i]);
				}
				fclose(fp);
#ifdef TRACE
				FILE *t_fp;
				char t_file_path[100] = "";
				int t_exist = 0;

				strcat(t_file_path, TRACE_PATH);
				strcat(t_file_path, TRACE_FILE);

				printf("trace path : %s\n", t_file_path);
				t_fp=fopen(t_file_path,"w+");

				if (t_fp == NULL) 
				{
					/* make directory */
					while(!t_exist){
						int t_result;

						t_result = mkdir(TRACE_PATH, 0766);

						if(t_result == 0) {
							/* success */
							t_exist = 1;

							t_fp=fopen(t_file_path,"w+");
						}
					}
				}

				for(int i = 0; i < trace_iter * iteration; i++) 
				{
					for(int k = 0; k < NUM_TRACE; k++) 
					{
						printf("%0.1f\n", trace_array[k][i]);
						switch(k){
							case 0:
								fprintf(t_fp, "%0.1f,", trace_array[k][i]);
								break;
							case 1:
								fprintf(t_fp, "%0.1f,", trace_array[k][i]);
								break;
							case 2:
								trace_array[k][i] /= 1000.0;
								fprintf(t_fp, "%0.1f,", trace_array[k][i]);
								break;
							case 3:
								trace_array[k][i] /= 1000.0;
								fprintf(t_fp, "%0.1f\n", trace_array[k][i]);
								break;
							default:
								break;
						}
						trace_data_sum[k] += trace_array[k][i];
					}
				}
				fclose(t_fp);
#endif

				/* exit loop */
				break;
			}
			cnt++;
			buff_index = (buff_index + 1) % 3;
			printf("================end===============\n");
		}
		cnt = 0;
	}
	
	//print average data
	for(int k=0; k<cycle;k++){
		printf("======== Darknet data ========\n");
		printf("avg_image_waiting[%d] : %f\n",k,image_waiting_sum[k]/iteration);
		printf("avg_fetch[%d] : %f\n",k,fetch_sum[k]/iteration);
		printf("avg_detect[%d] : %f\n",k,detect_sum[k]/iteration);
		printf("avg_display[%d] : %f\n",k,display_sum[k]/iteration);
		printf("avg_slack[%d] : %f\n",k,slack_sum[k]/iteration);
		printf("avg_fps[%d] : %f\n",k,fps_sum[k]/iteration);
		printf("avg_latency[%d] : %f\n",k,latency_sum[k]/iteration);
		printf("avg_select[%d] : %f\n",k,select_sum[k]/iteration);
		printf("avg_inter_frame_gap[%d] : %f\n",k,(double)inter_frame_gap_sum[k]/iteration);
#ifdef TRACE
		printf("======== Power & Temperature ========\n");
		printf("avg_gpu_power : %f\n", trace_data_sum[0]/(4*iteration));
		printf("avg_cpu_power : %f\n", trace_data_sum[1]/(4*iteration));
		printf("avg_gpu_temp : %f\n", trace_data_sum[2]/(4*iteration));
		printf("avg_cpu_temp : %f\n", trace_data_sum[3]/(4*iteration));
#endif

	}

    printf("input video stream closed. \n");
    if (output_video_writer) {
        release_video_writer(&output_video_writer);
        printf("output_video_writer closed. \n");
    }

    // free memory
	//printf("1\n");
    free_image(in_s);
	//printf("2\n");
    free_detections(dets, nboxes);
	//printf("3\n");

    free(avg);
	//printf("3.1\n");
    for (j = 0; j < NFRAMES; ++j) free(predictions[j]);
    demo_index = (NFRAMES + demo_index - 1) % NFRAMES;
	//printf("3.2\n");
    for (j = 0; j < NFRAMES; ++j) {
            release_mat(&cv_images[j]);
			printf("j = %d\n", j);
			printf("NAFRAMES = %d\n", NFRAMES);
    }
	printf("4\n");
    free_ptrs((void **)names, net.layers[net.n - 1].classes);
	printf("5\n");
    int i;
    const int nsize = 8;
    for (j = 0; j < nsize; ++j) {
        for (i = 32; i < 127; ++i) {
            free_image(alphabet[j][i]);
        }
        free(alphabet[j]);
    }
    free(alphabet);
    free_network(net);
    //cudaProfilerStop();
}
#else
void demo(char *cfgfile, char *weightfile, float thresh, float hier_thresh, int cam_index, const char *filename, char **names, int classes,
    int frame_skip, char *prefix, char *out_filename, int mjpeg_port, int json_port, int dont_show, int ext_output, int letter_box_in, int time_limit_sec, char *http_post_host,
    int benchmark, int benchmark_layers)
{
    fprintf(stderr, "Demo needs OpenCV for webcam images.\n");
}
#endif
