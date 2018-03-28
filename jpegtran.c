/*
 * jpegtran.c
 *
 * Copyright (C) 1995-2013, Thomas G. Lane, Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains a command-line user interface for JPEG transcoding.
 * It is very similar to cjpeg.c, and partly to djpeg.c, but provides
 * lossless transcoding between different JPEG file formats.  It also
 * provides some lossless and sort-of-lossless transformations of JPEG data.
 */

#include "cdjpeg.h"		/* Common decls for cjpeg/djpeg applications */
#include "transupp.h"		/* Support routines for jpegtran */
#include "jversion.h"		/* for version message */

#ifdef USE_CCOMMAND		/* command-line reader for Macintosh */
#ifdef __MWERKS__
#include <SIOUX.h>              /* Metrowerks needs this */
#include <console.h>		/* ... and this */
#endif
#ifdef THINK_C
#include <console.h>		/* Think declares it here */
#endif
#endif


/*
 * Argument-parsing code.
 * The switch parser is designed to be useful with DOS-style command line
 * syntax, ie, intermixed switches and file names, where only the switches
 * to the left of a given file name affect processing of that file.
 * The main program in this file doesn't actually use this capability...
 */

typedef int value_type;

struct arraylist {
  int size;
  value_type* data;
};

extern void arraylist_initial(struct arraylist *list);
extern int arraylist_get_size(const struct arraylist list);
extern value_type* arraylist_get_data_collection(const struct arraylist list);
extern void arraylist_set_data_collection(struct arraylist *list, value_type* data);
extern int arraylist_add(struct arraylist *list, value_type value);
extern value_type arraylist_get(const struct arraylist list, int index);
extern int arraylist_indexof(const struct arraylist list, value_type value);

void arraylist_initial(struct arraylist *list) {
  list->size = 0;
  list->data = NULL;
}

int arraylist_get_size(const struct arraylist list) {
  return list.size;
}

value_type* arraylist_get_data_collection(const struct arraylist list) {
  return list.data;
}

void arraylist_set_data_collection(struct arraylist *list, value_type* data) {
  list->data = data;
}

int arraylist_add(struct arraylist *list, value_type value) {
  int size = arraylist_get_size(*list);
  value_type *new_data;

  new_data = (value_type *)realloc(list->data, (size + 1) * sizeof new_data[0]);

  if (new_data)
  {
      new_data[size] = value;
      arraylist_set_data_collection(list, new_data);
      ++list->size;
  }
  return 1;
}


struct arraylist values(char *line)
{
	struct arraylist u;

	char* token = strtok(line, " ");
	while (token) {
		// srcX
		arraylist_add(&u, atoi(token));
		token = strtok(NULL, " ");
		// srcY
		arraylist_add(&u, atoi(token));
		token = strtok(NULL, " ");
		// destX
		arraylist_add(&u, atoi(token));
		token = strtok(NULL, " ");
		// destY
		arraylist_add(&u, atoi(token));
		token = strtok(NULL, " ");
		// width
		arraylist_add(&u, atoi(token));
		token = strtok(NULL, " ");
		// height
		arraylist_add(&u, atoi(token));

		// next change set
		token = strtok(NULL, " ");
	}
	return u;
}

static const char * progname;	/* program name for error messages */
static char * outfilename;	/* for -outfile switch */
static char * dropfilename;	/* for -drop switch */
static char * scaleoption;	/* -scale switch */
static JCOPY_OPTION copyoption;	/* -copy switch */
static jpeg_transform_info transformoption; /* image transformation options */

void do_crop(unsigned char *srcbuffer, long src_size, unsigned char **outbuffer, long *out_size, char *crop_spec);
void do_drop1(unsigned char *srcbuffer, long src_size, unsigned char *dropbuffer, long drop_size, unsigned char **outbuffer, long *out_size, char *writefile, char *crop_spec);
LOCAL(void)
usage (void)
/* complain about bad command line */
{
  fprintf(stderr, "usage: %s [switches] ", progname);
#ifdef TWO_FILE_COMMANDLINE
  fprintf(stderr, "inputfile outputfile\n");
#else
  fprintf(stderr, "[inputfile]\n");
#endif

  fprintf(stderr, "Switches (names may be abbreviated):\n");
  fprintf(stderr, "  -copy none     Copy no extra markers from source file\n");
  fprintf(stderr, "  -copy comments Copy only comment markers (default)\n");
  fprintf(stderr, "  -copy all      Copy all extra markers\n");
#ifdef ENTROPY_OPT_SUPPORTED
  fprintf(stderr, "  -optimize      Optimize Huffman table (smaller file, but slow compression)\n");
#endif
#ifdef C_PROGRESSIVE_SUPPORTED
  fprintf(stderr, "  -progressive   Create progressive JPEG file\n");
#endif
  fprintf(stderr, "Switches for modifying the image:\n");
#if TRANSFORMS_SUPPORTED
  fprintf(stderr, "  -crop WxH+X+Y  Crop to a rectangular subarea\n");
  fprintf(stderr, "  -drop +X+Y filename          Drop another image\n");
  fprintf(stderr, "  -flip [horizontal|vertical]  Mirror image (left-right or top-bottom)\n");
  fprintf(stderr, "  -grayscale     Reduce to grayscale (omit color data)\n");
  fprintf(stderr, "  -perfect       Fail if there is non-transformable edge blocks\n");
  fprintf(stderr, "  -rotate [90|180|270]         Rotate image (degrees clockwise)\n");
#endif
  fprintf(stderr, "  -scale M/N     Scale output image by fraction M/N, eg, 1/8\n");
#if TRANSFORMS_SUPPORTED
  fprintf(stderr, "  -transpose     Transpose image\n");
  fprintf(stderr, "  -transverse    Transverse transpose image\n");
  fprintf(stderr, "  -trim          Drop non-transformable edge blocks\n");
  fprintf(stderr, "                 with -drop: Requantize drop file to source file\n");
  fprintf(stderr, "  -wipe WxH+X+Y  Wipe (gray out) a rectangular subarea\n");
#endif
  fprintf(stderr, "Switches for advanced users:\n");
#ifdef C_ARITH_CODING_SUPPORTED
  fprintf(stderr, "  -arithmetic    Use arithmetic coding\n");
#endif
  fprintf(stderr, "  -restart N     Set restart interval in rows, or in blocks with B\n");
  fprintf(stderr, "  -maxmemory N   Maximum memory to use (in kbytes)\n");
  fprintf(stderr, "  -outfile name  Specify name for output file\n");
  fprintf(stderr, "  -verbose  or  -debug   Emit debug output\n");
  fprintf(stderr, "Switches for wizards:\n");
#ifdef C_MULTISCAN_FILES_SUPPORTED
  fprintf(stderr, "  -scans file    Create multi-scan JPEG per script file\n");
#endif
  exit(EXIT_FAILURE);
}


LOCAL(void)
select_transform (JXFORM_CODE transform)
/* Silly little routine to detect multiple transform options,
 * which we can't handle.
 */
{
#if TRANSFORMS_SUPPORTED
  if (transformoption.transform == JXFORM_NONE ||
      transformoption.transform == transform) {
    transformoption.transform = transform;
  } else {
    fprintf(stderr, "%s: can only do one image transformation at a time\n",
	    progname);
    usage();
  }
#else
  fprintf(stderr, "%s: sorry, image transformation was not compiled\n",
	  progname);
  exit(EXIT_FAILURE);
#endif
}


LOCAL(int)
parse_switches (j_compress_ptr cinfo, int argc, char **argv,
		int last_file_arg_seen, boolean for_real)
/* Parse optional switches.
 * Returns argv[] index of first file-name argument (== argc if none).
 * Any file names with indexes <= last_file_arg_seen are ignored;
 * they have presumably been processed in a previous iteration.
 * (Pass 0 for last_file_arg_seen on the first or only iteration.)
 * for_real is FALSE on the first (dummy) pass; we may skip any expensive
 * processing.
 */
{
  int argn;
  char * arg;
  boolean simple_progressive;
  char * scansarg = NULL;	/* saves -scans parm if any */

  /* Set up default JPEG parameters. */
  simple_progressive = FALSE;
  outfilename = NULL;
  scaleoption = NULL;
  copyoption = JCOPYOPT_DEFAULT;
  transformoption.transform = JXFORM_NONE;
  transformoption.perfect = FALSE;
  transformoption.trim = FALSE;
  transformoption.force_grayscale = FALSE;
  transformoption.crop = FALSE;
  cinfo->err->trace_level = 0;

  /* Scan command line options, adjust parameters */

  for (argn = 1; argn < argc; argn++) {
    arg = argv[argn];
    if (*arg != '-') {
      /* Not a switch, must be a file name argument */
      if (argn <= last_file_arg_seen) {
	outfilename = NULL;	/* -outfile applies to just one input file */
	continue;		/* ignore this name if previously processed */
      }
      break;			/* else done parsing switches */
    }
    arg++;			/* advance past switch marker character */

    if (keymatch(arg, "arithmetic", 1)) {
      /* Use arithmetic coding. */
#ifdef C_ARITH_CODING_SUPPORTED
      cinfo->arith_code = TRUE;
#else
      fprintf(stderr, "%s: sorry, arithmetic coding not supported\n",
	      progname);
      exit(EXIT_FAILURE);
#endif

    } else if (keymatch(arg, "copy", 2)) {
      /* Select which extra markers to copy. */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (keymatch(argv[argn], "none", 1)) {
	copyoption = JCOPYOPT_NONE;
      } else if (keymatch(argv[argn], "comments", 1)) {
	copyoption = JCOPYOPT_COMMENTS;
      } else if (keymatch(argv[argn], "all", 1)) {
	copyoption = JCOPYOPT_ALL;
      } else
	usage();

    } else if (keymatch(arg, "crop", 2)) {
      /* Perform lossless cropping. */
#if TRANSFORMS_SUPPORTED
      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (transformoption.crop /* reject multiple crop/drop/wipe requests */ ||
	  ! jtransform_parse_crop_spec(&transformoption, argv[argn])) {
	fprintf(stderr, "%s: bogus -crop argument '%s'\n",
		progname, argv[argn]);
	exit(EXIT_FAILURE);
      }
#else
      select_transform(JXFORM_NONE);	/* force an error */
#endif

    } else if (keymatch(arg, "drop", 2)) {
#if TRANSFORMS_SUPPORTED
      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (transformoption.crop /* reject multiple crop/drop/wipe requests */ ||
	  ! jtransform_parse_crop_spec(&transformoption, argv[argn]) ||
	  transformoption.crop_width_set != JCROP_UNSET ||
	  transformoption.crop_height_set != JCROP_UNSET) {
	fprintf(stderr, "%s: bogus -drop argument '%s'\n",
		progname, argv[argn]);
	exit(EXIT_FAILURE);
      }
      if (++argn >= argc)	/* advance to next argument */
	usage();
      dropfilename = argv[argn];
      select_transform(JXFORM_DROP);
#else
      select_transform(JXFORM_NONE);	/* force an error */
#endif

    } else if (keymatch(arg, "debug", 1) || keymatch(arg, "verbose", 1)) {
      /* Enable debug printouts. */
      /* On first -d, print version identification */
      static boolean printed_version = FALSE;

      if (! printed_version) {
	fprintf(stderr, "Independent JPEG Group's JPEGTRAN, version %s\n%s\n",
		JVERSION, JCOPYRIGHT);
	printed_version = TRUE;
      }
      cinfo->err->trace_level++;

    } else if (keymatch(arg, "flip", 1)) {
      /* Mirror left-right or top-bottom. */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (keymatch(argv[argn], "horizontal", 1))
	select_transform(JXFORM_FLIP_H);
      else if (keymatch(argv[argn], "vertical", 1))
	select_transform(JXFORM_FLIP_V);
      else
	usage();

    } else if (keymatch(arg, "grayscale", 1) || keymatch(arg, "greyscale",1)) {
      /* Force to grayscale. */
#if TRANSFORMS_SUPPORTED
      transformoption.force_grayscale = TRUE;
#else
      select_transform(JXFORM_NONE);	/* force an error */
#endif

    } else if (keymatch(arg, "maxmemory", 3)) {
      /* Maximum memory in Kb (or Mb with 'm'). */
      long lval;
      char ch = 'x';

      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (sscanf(argv[argn], "%ld%c", &lval, &ch) < 1)
	usage();
      if (ch == 'm' || ch == 'M')
	lval *= 1000L;
      cinfo->mem->max_memory_to_use = lval * 1000L;

    } else if (keymatch(arg, "optimize", 1) || keymatch(arg, "optimise", 1)) {
      /* Enable entropy parm optimization. */
#ifdef ENTROPY_OPT_SUPPORTED
      cinfo->optimize_coding = TRUE;
#else
      fprintf(stderr, "%s: sorry, entropy optimization was not compiled\n",
	      progname);
      exit(EXIT_FAILURE);
#endif

    } else if (keymatch(arg, "outfile", 4)) {
      /* Set output file name. */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      outfilename = argv[argn];	/* save it away for later use */

    } else if (keymatch(arg, "perfect", 2)) {
      /* Fail if there is any partial edge MCUs that the transform can't
       * handle. */
      transformoption.perfect = TRUE;

    } else if (keymatch(arg, "progressive", 2)) {
      /* Select simple progressive mode. */
#ifdef C_PROGRESSIVE_SUPPORTED
      simple_progressive = TRUE;
      /* We must postpone execution until num_components is known. */
#else
      fprintf(stderr, "%s: sorry, progressive output was not compiled\n",
	      progname);
      exit(EXIT_FAILURE);
#endif

    } else if (keymatch(arg, "restart", 1)) {
      /* Restart interval in MCU rows (or in MCUs with 'b'). */
      long lval;
      char ch = 'x';

      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (sscanf(argv[argn], "%ld%c", &lval, &ch) < 1)
	usage();
      if (lval < 0 || lval > 65535L)
	usage();
      if (ch == 'b' || ch == 'B') {
	cinfo->restart_interval = (unsigned int) lval;
	cinfo->restart_in_rows = 0; /* else prior '-restart n' overrides me */
      } else {
	cinfo->restart_in_rows = (int) lval;
	/* restart_interval will be computed during startup */
      }

    } else if (keymatch(arg, "rotate", 2)) {
      /* Rotate 90, 180, or 270 degrees (measured clockwise). */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (keymatch(argv[argn], "90", 2))
	select_transform(JXFORM_ROT_90);
      else if (keymatch(argv[argn], "180", 3))
	select_transform(JXFORM_ROT_180);
      else if (keymatch(argv[argn], "270", 3))
	select_transform(JXFORM_ROT_270);
      else
	usage();

    } else if (keymatch(arg, "scale", 4)) {
      /* Scale the output image by a fraction M/N. */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      scaleoption = argv[argn];
      /* We must postpone processing until decompression startup. */

    } else if (keymatch(arg, "scans", 1)) {
      /* Set scan script. */
#ifdef C_MULTISCAN_FILES_SUPPORTED
      if (++argn >= argc)	/* advance to next argument */
	usage();
      scansarg = argv[argn];
      /* We must postpone reading the file in case -progressive appears. */
#else
      fprintf(stderr, "%s: sorry, multi-scan output was not compiled\n",
	      progname);
      exit(EXIT_FAILURE);
#endif

    } else if (keymatch(arg, "transpose", 1)) {
      /* Transpose (across UL-to-LR axis). */
      select_transform(JXFORM_TRANSPOSE);

    } else if (keymatch(arg, "transverse", 6)) {
      /* Transverse transpose (across UR-to-LL axis). */
      select_transform(JXFORM_TRANSVERSE);

    } else if (keymatch(arg, "trim", 3)) {
      /* Trim off any partial edge MCUs that the transform can't handle. */
      transformoption.trim = TRUE;

    } else if (keymatch(arg, "wipe", 1)) {
#if TRANSFORMS_SUPPORTED
      if (++argn >= argc)	/* advance to next argument */
	usage();
      if (transformoption.crop /* reject multiple crop/drop/wipe requests */ ||
	  ! jtransform_parse_crop_spec(&transformoption, argv[argn])) {
	fprintf(stderr, "%s: bogus -wipe argument '%s'\n",
		progname, argv[argn]);
	exit(EXIT_FAILURE);
      }
      select_transform(JXFORM_WIPE);
#else
      select_transform(JXFORM_NONE);	/* force an error */
#endif

    } else {
      usage();			/* bogus switch */
    }
  }

  /* Post-switch-scanning cleanup */

  if (for_real) {

#ifdef C_PROGRESSIVE_SUPPORTED
    if (simple_progressive)	/* process -progressive; -scans can override */
      jpeg_simple_progression(cinfo);
#endif

#ifdef C_MULTISCAN_FILES_SUPPORTED
    if (scansarg != NULL)	/* process -scans if it was present */
      if (! read_scan_script(cinfo, scansarg))
	usage();
#endif
  }

  return argn;			/* return index of next arg (file name) */
}

static struct arraylist a;
/*
 * The main program.
 */
int
main (int argc, char **argv)
{
  int number = 0;
  int max;
  unsigned char *drop_img = NULL;
  long drop_size;
  unsigned char *out_img = NULL;
  long out_size;
  unsigned char *src_img = NULL;
  char cropspec[100];

  int *decoder;

  FILE *f;

  char *line = NULL;
  size_t size;
  getline(&line, &size, stdin);
  puts(line);
  a = values(line);

  puts("HERE");
  decoder = a.data;
  max = a.size;

  // read the image
  f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END);
  out_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  out_img = (unsigned char *)malloc(out_size + 1);
  src_img = out_img;
  fread(out_img, out_size, 1, f);
  fclose(f);

  while (number < max) {
	unsigned char *temp_img = NULL;
	long temp_size;

	sprintf(cropspec, "+%d+%d", decoder[number], decoder[number+1]);
	do_drop1(out_img, out_size, src_img, out_size, &temp_img, &temp_size, NULL, cropspec);
	free(drop_img);
	drop_img = NULL;
	if (number > 5) free(out_img);
	out_img = temp_img;
	out_size = temp_size;
	//printf("%dx%d+%d+%d\n", decoder[number+4], decoder[number+5], decoder[number+2], decoder[number+3]);
	printf(cropspec);
	//printf("\n");
	number += 6; // width, height, srcX, srcY, destX, destY
	//printf("Got here: %d %d", max, number);
	break;
  }

  f = fopen(argv[2], "wb");
  // for (number = 0; number < out_size; number++) {
  fwrite(out_img, 1, out_size, f);
  // }
	  fclose(f);
  // free(drop_img);
  free(out_img);

  return 0;
}

void do_crop(unsigned char *srcbuffer, long src_size, unsigned char **outbuffer, long *out_size, char *crop_spec)
{
  struct jpeg_decompress_struct srcinfo;
  struct jpeg_error_mgr jsrcerr;
  struct jpeg_decompress_struct dropinfo;
  struct jpeg_error_mgr jdroperr;
  struct jpeg_compress_struct dstinfo;
  struct jpeg_error_mgr jdsterr;
#ifdef PROGRESS_REPORT
  struct cdjpeg_progress_mgr progress;
#endif
  jvirt_barray_ptr * src_coef_arrays;
  jvirt_barray_ptr * dst_coef_arrays;
  int file_index;

  /* Initialize the JPEG decompression object with default error handling. */
  srcinfo.err = jpeg_std_error(&jsrcerr);
  jpeg_create_decompress(&srcinfo);
  /* Initialize the JPEG compression object with default error handling. */
  dstinfo.err = jpeg_std_error(&jdsterr);
  jpeg_create_compress(&dstinfo);

  /* Now safe to enable signal catcher.
   * Note: we assume only the decompression object will have virtual arrays.
   */
#ifdef NEED_SIGNAL_CATCHER
  enable_signal_catcher((j_common_ptr) &srcinfo);
#endif

  file_index = parse_switches(&dstinfo, 0, NULL, 0, FALSE);
  jtransform_parse_crop_spec(&transformoption, crop_spec);
  //transformoption.transform = JXFORM_CROP;
  transformoption.perfect = TRUE;
  jsrcerr.trace_level = jdsterr.trace_level;
  srcinfo.mem->max_memory_to_use = dstinfo.mem->max_memory_to_use;

#ifdef PROGRESS_REPORT
  start_progress_monitor((j_common_ptr) &dstinfo, &progress);
#endif

  /* Specify data source for decompression */
  //jpeg_stdio_src(&srcinfo, fp);
  jpeg_mem_src(&srcinfo, srcbuffer, src_size);

  /* Enable saving of extra markers that we want to copy */
  jcopy_markers_setup(&srcinfo, copyoption);

  /* Read file header */
  (void) jpeg_read_header(&srcinfo, TRUE);

  /* Fail right away if -perfect is given and transformation is not perfect.
   */
  if (!jtransform_request_workspace(&srcinfo, &transformoption)) {
    fprintf(stderr, "%s: transformation is not perfect\n", progname);
    exit(EXIT_FAILURE);
  }

  /* Read source file as DCT coefficients */
  src_coef_arrays = jpeg_read_coefficients(&srcinfo);

  /* Initialize destination compression parameters from source values */
  jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

  /* Adjust destination parameters if required by transform options;
   * also find out which set of coefficient arrays will hold the output.
   */
  dst_coef_arrays = jtransform_adjust_parameters(&srcinfo, &dstinfo,
						 src_coef_arrays,
						 &transformoption);

  /* Adjust default compression parameters by re-parsing the options */
  file_index = parse_switches(&dstinfo, 0, NULL, 0, TRUE);
  jtransform_parse_crop_spec(&transformoption, crop_spec);
 // transformoption.transform = JXFORM_CROP;
  transformoption.perfect = TRUE;

  /* Specify data destination for compression */
  //jpeg_stdio_dest(&dstinfo, fp);
  jpeg_mem_dest(&dstinfo, outbuffer, (unsigned long *)out_size);

  /* Start compressor (note no image data is actually written here) */
  jpeg_write_coefficients(&dstinfo, dst_coef_arrays);

  /* Copy to the output file any extra markers that we want to preserve */
  jcopy_markers_execute(&srcinfo, &dstinfo, copyoption);

  /* Execute image transformation, if any */
  jtransform_execute_transformation(&srcinfo, &dstinfo,
				    src_coef_arrays,
				    &transformoption);

  /* Finish compression and release memory */
  jpeg_finish_compress(&dstinfo);
  jpeg_destroy_compress(&dstinfo);

  (void) jpeg_finish_decompress(&srcinfo);
  jpeg_destroy_decompress(&srcinfo);


#ifdef PROGRESS_REPORT
  end_progress_monitor((j_common_ptr) &dstinfo);
#endif
}

void do_drop1(unsigned char *srcbuffer, long src_size, unsigned char *dropbuffer, long drop_size, unsigned char **outbuffer, long *out_size, char *writefile, char *crop_spec)
{
  struct jpeg_decompress_struct srcinfo;
  struct jpeg_error_mgr jsrcerr;
  struct jpeg_decompress_struct dropinfo;
  struct jpeg_error_mgr jdroperr;
  FILE * drop_file;
  struct jpeg_compress_struct dstinfo;
  struct jpeg_error_mgr jdsterr;
#ifdef PROGRESS_REPORT
  struct cdjpeg_progress_mgr progress;
#endif
  jvirt_barray_ptr * src_coef_arrays;
  jvirt_barray_ptr * dst_coef_arrays;
  int file_index;
  int crop1, crop2;

  FILE * fp;

  /* Initialize the JPEG decompression object with default error handling. */
  srcinfo.err = jpeg_std_error(&jsrcerr);
  jpeg_create_decompress(&srcinfo);
  /* Initialize the JPEG compression object with default error handling. */
  dstinfo.err = jpeg_std_error(&jdsterr);
  jpeg_create_compress(&dstinfo);

  /* Now safe to enable signal catcher.
   * Note: we assume only the decompression object will have virtual arrays.
   */
#ifdef NEED_SIGNAL_CATCHER
  enable_signal_catcher((j_common_ptr) &srcinfo);
#endif

  file_index = parse_switches(&dstinfo, 0, NULL, 0, FALSE);

  jtransform_parse_crop_spec(&transformoption, crop_spec);
  transformoption.transform = JXFORM_DROP;
  transformoption.perfect = TRUE;

  jsrcerr.trace_level = jdsterr.trace_level;
  srcinfo.mem->max_memory_to_use = dstinfo.mem->max_memory_to_use;

  dropinfo.err = jpeg_std_error(&jdroperr);
  jpeg_create_decompress(&dropinfo);
  //jpeg_stdio_src(&dropinfo, drop_file);
  jpeg_mem_src(&dropinfo, dropbuffer, drop_size);

#ifdef PROGRESS_REPORT
  start_progress_monitor((j_common_ptr) &dstinfo, &progress);
#endif

  /* Specify data source for decompression */
  //jpeg_stdio_src(&srcinfo, fp);
  jpeg_mem_src(&srcinfo, srcbuffer, src_size);

  /* Enable saving of extra markers that we want to copy */
  jcopy_markers_setup(&srcinfo, copyoption);

  /* Read file header */
  (void) jpeg_read_header(&srcinfo, TRUE);

  (void) jpeg_read_header(&dropinfo, TRUE);
  transformoption.crop_width = 64;
  transformoption.crop_width_set = JCROP_POS;
  transformoption.crop_height = 64;
  transformoption.crop_height_set = JCROP_POS;
  transformoption.drop_ptr = &dropinfo;

  /* Fail right away if -perfect is given and transformation is not perfect.
   */
  if (!jtransform_request_workspace(&srcinfo, &transformoption)) {
    fprintf(stderr, "%s: transformation is not perfect\n", progname);
    exit(EXIT_FAILURE);
  }

  /* Read source file as DCT coefficients */
  src_coef_arrays = jpeg_read_coefficients(&srcinfo);

  transformoption.drop_coef_arrays = jpeg_read_coefficients(&dropinfo);


  /* Initialize destination compression parameters from source values */
  jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

  /* Adjust destination parameters if required by transform options;
   * also find out which set of coefficient arrays will hold the output.
   */
  dst_coef_arrays = jtransform_adjust_parameters(&srcinfo, &dstinfo,
						 src_coef_arrays,
						 &transformoption);


  /* Open the output file. */
  if (writefile != NULL) {
    if ((fp = fopen(writefile, WRITE_BINARY)) == NULL) {
      fprintf(stderr, "%s: can't open %s for writing\n", progname, outfilename);
      exit(EXIT_FAILURE);
    }
  } else {
    /* default output file is stdout */
    fp = write_stdout();
  }

  // END OPTIONAL FILE

  /* Adjust default compression parameters by re-parsing the options */
  file_index = parse_switches(&dstinfo, 0, NULL, 0, TRUE);
  jtransform_parse_crop_spec(&transformoption, crop_spec);
  transformoption.transform = JXFORM_DROP;
  transformoption.perfect = TRUE;

  /* Specify data destination for compression */

  if (writefile != NULL) {
	 jpeg_stdio_dest(&dstinfo, fp);
  } else {
    jpeg_mem_dest(&dstinfo, outbuffer, (unsigned long *)out_size);
  }


  /* Start compressor (note no image data is actually written here) */
  jpeg_write_coefficients(&dstinfo, dst_coef_arrays);

  /* Copy to the output file any extra markers that we want to preserve */
  jcopy_markers_execute(&srcinfo, &dstinfo, copyoption);

  /* Execute image transformation, if any */
  // jtransform_execute_transformation(&srcinfo, &dstinfo,
  //				    src_coef_arrays,
  //				    &transformoption);
  //printf("\nTHIS%d\n", transformoption.drop_height);
  {
	  int number = 0;
	  int *decoder = a.data;

	  while (number < a.size) {
		  long temp_size;
		  int crop_width, crop_height, srcX, srcY, destX, destY;
		  char cropspec[100];
		  crop_width = decoder[number+4];
		  crop_height = decoder[number+5];
		  srcX = decoder[number+2];
		  srcY = decoder[number+3];

		  destX = decoder[number];
		  destY = decoder[number+1];

		  // continually run the src coefficient array again
		  // with new drop coefficient array and positions
		  // and offsets
		  sprintf(cropspec, "+%d+%d", srcX, srcY);

		  //  transformoption.perfect = TRUE;


		  jtransform_parse_crop_spec(&transformoption, cropspec);
		  transformoption.crop_width = 32;
		  transformoption.crop_width_set = JCROP_POS;
		  transformoption.crop_height = 32;
		  transformoption.crop_height_set = JCROP_POS;

		  {
			  JDIMENSION xoffset, yoffset, dtemp;
			  JDIMENSION width_in_iMCUs, height_in_iMCUs;
			  JDIMENSION width_in_blocks, height_in_blocks;
			  int itemp, ci, h_samp_factor, v_samp_factor;

			  if (transformoption.perfect) {
				  if (transformoption.num_components == 1) {
					  if (!jtransform_perfect_transform(transformoption.output_width,
						  srcinfo.output_height,
						  srcinfo.min_DCT_h_scaled_size,
						  srcinfo.min_DCT_v_scaled_size,
						  transformoption.transform))
						  return;
				  } else {
					  if (!jtransform_perfect_transform(srcinfo.output_width,
						  srcinfo.output_height,
						  srcinfo.max_h_samp_factor * srcinfo.min_DCT_h_scaled_size,
						  srcinfo.max_v_samp_factor * srcinfo.min_DCT_v_scaled_size,
						  transformoption.transform))
						  return;
				  }
			  }

			  /* If there is only one output component, force the iMCU size to be 1;
			  * else use the source iMCU size.  (This allows us to do the right thing
			  * when reducing color to grayscale, and also provides a handy way of
			  * cleaning up "funny" grayscale images whose sampling factors are not 1x1.)
			  */
			  switch (transformoption.transform) {
			  case JXFORM_TRANSPOSE:
			  case JXFORM_TRANSVERSE:
			  case JXFORM_ROT_90:
			  case JXFORM_ROT_270:
				  transformoption.output_width = srcinfo.output_height;
				  transformoption.output_height = srcinfo.output_width;
				  if (transformoption.num_components == 1) {
					  transformoption.iMCU_sample_width = srcinfo.min_DCT_v_scaled_size;
					  transformoption.iMCU_sample_height = srcinfo.min_DCT_h_scaled_size;
				  } else {
					  transformoption.iMCU_sample_width =
						  srcinfo.max_v_samp_factor * srcinfo.min_DCT_v_scaled_size;
					  transformoption.iMCU_sample_height =
						  srcinfo.max_h_samp_factor * srcinfo.min_DCT_h_scaled_size;
				  }
				  break;
			  default:
				  transformoption.output_width = srcinfo.output_width;
				  transformoption.output_height = srcinfo.output_height;
				  if (transformoption.num_components == 1) {
					  transformoption.iMCU_sample_width = srcinfo.min_DCT_h_scaled_size;
					  transformoption.iMCU_sample_height = srcinfo.min_DCT_v_scaled_size;
				  } else {
					  transformoption.iMCU_sample_width =
						  srcinfo.max_h_samp_factor * srcinfo.min_DCT_h_scaled_size;
					  transformoption.iMCU_sample_height =
						  srcinfo.max_v_samp_factor * srcinfo.min_DCT_v_scaled_size;
				  }
				  break;
			  }

			  /* If cropping has been requested, compute the crop area's position and
			  * dimensions, ensuring that its upper left corner falls at an iMCU boundary.
			  */
			  if (transformoption.crop) {
				  /* Insert default values for unset crop parameters */
				  if (transformoption.crop_xoffset_set == JCROP_UNSET)
					  transformoption.crop_xoffset = 0;	/* default to +0 */
				  if (transformoption.crop_yoffset_set == JCROP_UNSET)
					  transformoption.crop_yoffset = 0;	/* default to +0 */
				  if (transformoption.crop_width_set == JCROP_UNSET) {
					  if (transformoption.crop_xoffset >= transformoption.output_width)
						  return;
					  transformoption.crop_width = transformoption.output_width - transformoption.crop_xoffset;
				  } else {
					  /* Check for crop extension */
					  if (transformoption.crop_width > transformoption.output_width) {
						  /* Crop extension does not work when transforming! */
						  if (transformoption.transform != JXFORM_NONE ||
							  transformoption.crop_xoffset >= transformoption.crop_width ||
							  transformoption.crop_xoffset > transformoption.crop_width - transformoption.output_width)
							  return;
					  } else {
						  if (transformoption.crop_xoffset >= transformoption.output_width ||
							  transformoption.crop_width <= 0 ||
							  transformoption.crop_xoffset > transformoption.output_width - transformoption.crop_width)
							  return;
					  }
				  }
				  if (transformoption.crop_height_set == JCROP_UNSET) {
					  if (transformoption.crop_yoffset >= transformoption.output_height)
						  return;
					  transformoption.crop_height = transformoption.output_height - transformoption.crop_yoffset;
				  } else {
					  /* Check for crop extension */
					  if (transformoption.crop_height > transformoption.output_height) {
						  /* Crop extension does not work when transforming! */
						  if (transformoption.transform != JXFORM_NONE ||
							  transformoption.crop_yoffset >= transformoption.crop_height ||
							  transformoption.crop_yoffset > transformoption.crop_height - transformoption.output_height)
							  return;
					  } else {
						  if (transformoption.crop_yoffset >= transformoption.output_height ||
							  transformoption.crop_height <= 0 ||
							  transformoption.crop_yoffset > transformoption.output_height - transformoption.crop_height)
							  return;
					  }
				  }
				  /* Convert negative crop offsets into regular offsets */
				  if (transformoption.crop_xoffset_set != JCROP_NEG)
					  xoffset = transformoption.crop_xoffset;
				  else if (transformoption.crop_width > transformoption.output_width) /* crop extension */
					  xoffset = transformoption.crop_width - transformoption.output_width - transformoption.crop_xoffset;
				  else
					  xoffset = transformoption.output_width - transformoption.crop_width - transformoption.crop_xoffset;
				  if (transformoption.crop_yoffset_set != JCROP_NEG)
					  yoffset = transformoption.crop_yoffset;
				  else if (transformoption.crop_height > transformoption.output_height) /* crop extension */
					  yoffset = transformoption.crop_height - transformoption.output_height - transformoption.crop_yoffset;
				  else
					  yoffset = transformoption.output_height - transformoption.crop_height - transformoption.crop_yoffset;
				  /* Now adjust so that upper left corner falls at an iMCU boundary */
				  switch (transformoption.transform) {
				  case JXFORM_DROP:
					  /* Ensure the effective drop region will not exceed the requested */
					  itemp = transformoption.iMCU_sample_width;
					  dtemp = itemp - 1 - ((xoffset + itemp - 1) % itemp);
					  xoffset += dtemp;
					  if (transformoption.crop_width <= dtemp)
						  transformoption.drop_width = 0;
					  else if (xoffset + transformoption.crop_width - dtemp == transformoption.output_width)
						  /* Matching right edge: include partial iMCU */
						  transformoption.drop_width = (transformoption.crop_width - dtemp + itemp - 1) / itemp;
					  else
						  transformoption.drop_width = (transformoption.crop_width - dtemp) / itemp;
					  itemp = transformoption.iMCU_sample_height;
					  dtemp = itemp - 1 - ((yoffset + itemp - 1) % itemp);
					  yoffset += dtemp;
					  if (transformoption.crop_height <= dtemp)
						  transformoption.drop_height = 0;
					  else if (yoffset + transformoption.crop_height - dtemp == transformoption.output_height)
						  /* Matching bottom edge: include partial iMCU */
						  transformoption.drop_height = (transformoption.crop_height - dtemp + itemp - 1) / itemp;
					  else
						  transformoption.drop_height = (transformoption.crop_height - dtemp) / itemp;
					  /* Check if sampling factors match for dropping */
					  if (transformoption.drop_width != 0 && transformoption.drop_height != 0)
						  for (ci = 0; ci < transformoption.num_components &&
							  ci < transformoption.drop_ptr->num_components; ci++) {
								  if (transformoption.drop_ptr->comp_info[ci].h_samp_factor *
									  srcinfo.max_h_samp_factor !=
									  srcinfo.comp_info[ci].h_samp_factor *
									  transformoption.drop_ptr->max_h_samp_factor)
									  return;
								  if (transformoption.drop_ptr->comp_info[ci].v_samp_factor *
									  srcinfo.max_v_samp_factor !=
									  srcinfo.comp_info[ci].v_samp_factor *
									  transformoption.drop_ptr->max_v_samp_factor)
									  return;
						  }
						  break;
				  default:
					  /* Ensure the effective crop region will cover the requested */
					  if (transformoption.crop_width_set == JCROP_FORCE ||
						  transformoption.crop_width > transformoption.output_width)
						  transformoption.output_width = transformoption.crop_width;
					  else
						  transformoption.output_width =
						  transformoption.crop_width + (xoffset % transformoption.iMCU_sample_width);
					  if (transformoption.crop_height_set == JCROP_FORCE ||
						  transformoption.crop_height > transformoption.output_height)
						  transformoption.output_height = transformoption.crop_height;
					  else
						  transformoption.output_height =
						  transformoption.crop_height + (yoffset % transformoption.iMCU_sample_height);
					  break;
				  }
				  /* Save x/y offsets measured in iMCUs */
				  transformoption.x_crop_offset = xoffset / transformoption.iMCU_sample_width;
				  transformoption.y_crop_offset = yoffset / transformoption.iMCU_sample_height;
			  } else {
				  transformoption.x_crop_offset = 0;
				  transformoption.y_crop_offset = 0;
			  }

		  }

		  sprintf(cropspec, "+%d+%d", destX, destY);
		  //  transformoption.perfect = TRUE;

		  crop2 = transformoption.y_crop_offset;
		  crop1 = transformoption.x_crop_offset;
		  // second
		  jtransform_parse_crop_spec(&transformoption, cropspec);
		  transformoption.crop_width = crop_width;
		  transformoption.crop_width_set = JCROP_POS;
		  transformoption.crop_height = crop_height;
		  transformoption.crop_height_set = JCROP_POS;

		  {
			  JDIMENSION xoffset, yoffset, dtemp;
			  JDIMENSION width_in_iMCUs, height_in_iMCUs;
			  JDIMENSION width_in_blocks, height_in_blocks;
			  int itemp, ci, h_samp_factor, v_samp_factor;

			  if (transformoption.perfect) {
				  if (transformoption.num_components == 1) {
					  if (!jtransform_perfect_transform(transformoption.output_width,
						  srcinfo.output_height,
						  srcinfo.min_DCT_h_scaled_size,
						  srcinfo.min_DCT_v_scaled_size,
						  transformoption.transform))
						  return;
				  } else {
					  if (!jtransform_perfect_transform(srcinfo.output_width,
						  srcinfo.output_height,
						  srcinfo.max_h_samp_factor * srcinfo.min_DCT_h_scaled_size,
						  srcinfo.max_v_samp_factor * srcinfo.min_DCT_v_scaled_size,
						  transformoption.transform))
						  return;
				  }
			  }

			  /* If there is only one output component, force the iMCU size to be 1;
			  * else use the source iMCU size.  (This allows us to do the right thing
			  * when reducing color to grayscale, and also provides a handy way of
			  * cleaning up "funny" grayscale images whose sampling factors are not 1x1.)
			  */
			  switch (transformoption.transform) {
			  case JXFORM_TRANSPOSE:
			  case JXFORM_TRANSVERSE:
			  case JXFORM_ROT_90:
			  case JXFORM_ROT_270:
				  transformoption.output_width = srcinfo.output_height;
				  transformoption.output_height = srcinfo.output_width;
				  if (transformoption.num_components == 1) {
					  transformoption.iMCU_sample_width = srcinfo.min_DCT_v_scaled_size;
					  transformoption.iMCU_sample_height = srcinfo.min_DCT_h_scaled_size;
				  } else {
					  transformoption.iMCU_sample_width =
						  srcinfo.max_v_samp_factor * srcinfo.min_DCT_v_scaled_size;
					  transformoption.iMCU_sample_height =
						  srcinfo.max_h_samp_factor * srcinfo.min_DCT_h_scaled_size;
				  }
				  break;
			  default:
				  transformoption.output_width = srcinfo.output_width;
				  transformoption.output_height = srcinfo.output_height;
				  if (transformoption.num_components == 1) {
					  transformoption.iMCU_sample_width = srcinfo.min_DCT_h_scaled_size;
					  transformoption.iMCU_sample_height = srcinfo.min_DCT_v_scaled_size;
				  } else {
					  transformoption.iMCU_sample_width =
						  srcinfo.max_h_samp_factor * srcinfo.min_DCT_h_scaled_size;
					  transformoption.iMCU_sample_height =
						  srcinfo.max_v_samp_factor * srcinfo.min_DCT_v_scaled_size;
				  }
				  break;
			  }

			  /* If cropping has been requested, compute the crop area's position and
			  * dimensions, ensuring that its upper left corner falls at an iMCU boundary.
			  */
			  if (transformoption.crop) {
				  /* Insert default values for unset crop parameters */
				  if (transformoption.crop_xoffset_set == JCROP_UNSET)
					  transformoption.crop_xoffset = 0;	/* default to +0 */
				  if (transformoption.crop_yoffset_set == JCROP_UNSET)
					  transformoption.crop_yoffset = 0;	/* default to +0 */
				  if (transformoption.crop_width_set == JCROP_UNSET) {
					  if (transformoption.crop_xoffset >= transformoption.output_width)
						  return;
					  transformoption.crop_width = transformoption.output_width - transformoption.crop_xoffset;
				  } else {
					  /* Check for crop extension */
					  if (transformoption.crop_width > transformoption.output_width) {
						  /* Crop extension does not work when transforming! */
						  if (transformoption.transform != JXFORM_NONE ||
							  transformoption.crop_xoffset >= transformoption.crop_width ||
							  transformoption.crop_xoffset > transformoption.crop_width - transformoption.output_width)
							  return;
					  } else {
						  if (transformoption.crop_xoffset >= transformoption.output_width ||
							  transformoption.crop_width <= 0 ||
							  transformoption.crop_xoffset > transformoption.output_width - transformoption.crop_width)
							  return;
					  }
				  }
				  if (transformoption.crop_height_set == JCROP_UNSET) {
					  if (transformoption.crop_yoffset >= transformoption.output_height)
						  return;
					  transformoption.crop_height = transformoption.output_height - transformoption.crop_yoffset;
				  } else {
					  /* Check for crop extension */
					  if (transformoption.crop_height > transformoption.output_height) {
						  /* Crop extension does not work when transforming! */
						  if (transformoption.transform != JXFORM_NONE ||
							  transformoption.crop_yoffset >= transformoption.crop_height ||
							  transformoption.crop_yoffset > transformoption.crop_height - transformoption.output_height)
							  return;
					  } else {
						  if (transformoption.crop_yoffset >= transformoption.output_height ||
							  transformoption.crop_height <= 0 ||
							  transformoption.crop_yoffset > transformoption.output_height - transformoption.crop_height)
							  return;
					  }
				  }
				  /* Convert negative crop offsets into regular offsets */
				  if (transformoption.crop_xoffset_set != JCROP_NEG)
					  xoffset = transformoption.crop_xoffset;
				  else if (transformoption.crop_width > transformoption.output_width) /* crop extension */
					  xoffset = transformoption.crop_width - transformoption.output_width - transformoption.crop_xoffset;
				  else
					  xoffset = transformoption.output_width - transformoption.crop_width - transformoption.crop_xoffset;
				  if (transformoption.crop_yoffset_set != JCROP_NEG)
					  yoffset = transformoption.crop_yoffset;
				  else if (transformoption.crop_height > transformoption.output_height) /* crop extension */
					  yoffset = transformoption.crop_height - transformoption.output_height - transformoption.crop_yoffset;
				  else
					  yoffset = transformoption.output_height - transformoption.crop_height - transformoption.crop_yoffset;
				  /* Now adjust so that upper left corner falls at an iMCU boundary */
				  switch (transformoption.transform) {
				  case JXFORM_DROP:
					  /* Ensure the effective drop region will not exceed the requested */
					  itemp = transformoption.iMCU_sample_width;
					  dtemp = itemp - 1 - ((xoffset + itemp - 1) % itemp);
					  xoffset += dtemp;
					  if (transformoption.crop_width <= dtemp)
						  transformoption.drop_width = 0;
					  else if (xoffset + transformoption.crop_width - dtemp == transformoption.output_width)
						  /* Matching right edge: include partial iMCU */
						  transformoption.drop_width = (transformoption.crop_width - dtemp + itemp - 1) / itemp;
					  else
						  transformoption.drop_width = (transformoption.crop_width - dtemp) / itemp;
					  itemp = transformoption.iMCU_sample_height;
					  dtemp = itemp - 1 - ((yoffset + itemp - 1) % itemp);
					  yoffset += dtemp;
					  if (transformoption.crop_height <= dtemp)
						  transformoption.drop_height = 0;
					  else if (yoffset + transformoption.crop_height - dtemp == transformoption.output_height)
						  /* Matching bottom edge: include partial iMCU */
						  transformoption.drop_height = (transformoption.crop_height - dtemp + itemp - 1) / itemp;
					  else
						  transformoption.drop_height = (transformoption.crop_height - dtemp) / itemp;
					  /* Check if sampling factors match for dropping */
					  if (transformoption.drop_width != 0 && transformoption.drop_height != 0)
						  for (ci = 0; ci < transformoption.num_components &&
							  ci < transformoption.drop_ptr->num_components; ci++) {
								  if (transformoption.drop_ptr->comp_info[ci].h_samp_factor *
									  srcinfo.max_h_samp_factor !=
									  srcinfo.comp_info[ci].h_samp_factor *
									  transformoption.drop_ptr->max_h_samp_factor)
									  return;
								  if (transformoption.drop_ptr->comp_info[ci].v_samp_factor *
									  srcinfo.max_v_samp_factor !=
									  srcinfo.comp_info[ci].v_samp_factor *
									  transformoption.drop_ptr->max_v_samp_factor)
									  return;
						  }
						  break;
				  default:
					  /* Ensure the effective crop region will cover the requested */
					  if (transformoption.crop_width_set == JCROP_FORCE ||
						  transformoption.crop_width > transformoption.output_width)
						  transformoption.output_width = transformoption.crop_width;
					  else
						  transformoption.output_width =
						  transformoption.crop_width + (xoffset % transformoption.iMCU_sample_width);
					  if (transformoption.crop_height_set == JCROP_FORCE ||
						  transformoption.crop_height > transformoption.output_height)
						  transformoption.output_height = transformoption.crop_height;
					  else
						  transformoption.output_height =
						  transformoption.crop_height + (yoffset % transformoption.iMCU_sample_height);
					  break;
				  }
				  /* Save x/y offsets measured in iMCUs */
				  transformoption.x_crop_offset = xoffset / transformoption.iMCU_sample_width;
				  transformoption.y_crop_offset = yoffset / transformoption.iMCU_sample_height;
			  } else {
				  transformoption.x_crop_offset = 0;
				  transformoption.y_crop_offset = 0;
			  }

		  }

		  //printf("dfsdfs%d %d", transformoption.x_crop_offset, transformoption.y_crop_offset);
		  do_drop(&srcinfo, &dstinfo, transformoption.x_crop_offset, transformoption.y_crop_offset,
			  src_coef_arrays, &dropinfo, transformoption.drop_coef_arrays, transformoption.drop_width,
			  transformoption.drop_height, crop1, crop2);

		  number += 6;
	  }

  }

  /* Finish compression and release memory */
  jpeg_finish_compress(&dstinfo);
  jpeg_destroy_compress(&dstinfo);
  (void) jpeg_finish_decompress(&dropinfo);
  jpeg_destroy_decompress(&dropinfo);


  (void) jpeg_finish_decompress(&srcinfo);
  jpeg_destroy_decompress(&srcinfo);

#ifdef PROGRESS_REPORT
  end_progress_monitor((j_common_ptr) &dstinfo);
#endif

  if (writefile != NULL && fp != stdout)
    fclose(fp);

}
