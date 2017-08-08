//	Python MEF Wrapper to Mayo mef_lib_2_1 library





#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mef.h"
#include "mef_lib.c"

void initpymef2(void);


int main(int argc, char *argv[])
{
    /* Pass argv[0] to the Python interpreter */
    Py_SetProgramName(argv[0]);

    /* Initialize the Python interpreter.  Required. */
    Py_Initialize();

    /* Add a static module */
    initpymef2();

    Py_Exit(0);
 }

static PyObject* mef_to_raw(PyObject *self, PyObject *args) {
	si4 i, num, numBlocks, l, blocks_per_cycle, start_block, end_block;
	si4 *data, *dp;
	ui8 numEntries, inDataLength, outDataLength, bytesDecoded, entryCounter;
	long previous_block_start_time;
	si1 password[16], outFileName[200], path[200], *diff_buffer, *dbp;
	ui1 *hdr_block, *in_data, *idp,  encryptionKey[240];
	FILE *fp, *out_fp;
	MEF_HEADER_INFO header;
	RED_BLOCK_HDR_INFO RED_bk_hdr;
	INDEX_DATA *indx_array;

	blocks_per_cycle = 5000;
	memset(password, 0, 16);
	
	numEntries=0;

	char *fpath;
	char *suffix;

	PyArg_ParseTuple(args, "ss", &fpath,&suffix);
	
	/*if (argc < 2 || argc > 4) 
	{
		(void) printf("USAGE: %s file_name [password] \n", argv[0]);
		return Py_BuildValue("i", 1);
	}
		
	if (argc > 2) { //check input arguments for password
		strncpy(password, argv[2], 16);
	}*/
	
	//allocate memory for (encrypted) header block
	hdr_block = calloc(MEF_HEADER_LENGTH, sizeof(ui1));
	
	fp = fopen(fpath, "r");
	if (fp == NULL) {
		fprintf(stderr, "Error opening file %s\n", fpath);
		return Py_BuildValue("i", 1);
	}
	
	num = fread(hdr_block, 1, MEF_HEADER_LENGTH, fp);
	if (num != MEF_HEADER_LENGTH) {
		fprintf(stderr, "Error reading file %s\n", fpath);
		return Py_BuildValue("i", 1);
	}
		
	read_mef_header_block(hdr_block, &header, password);

	if (header.session_encryption_used && validate_password(hdr_block, password)==0) {
		fprintf(stderr, "Can not decrypt MEF header\n");
		free(hdr_block);
		return Py_BuildValue("i", 1);
	}

	numBlocks = header.number_of_index_entries;
	numEntries = header.number_of_samples;

	//	inDataLength = header.index_data_offset - header.header_length;
	inDataLength = blocks_per_cycle * header.maximum_compressed_block_size;
	if (header.data_encryption_used) {
		AES_KeyExpansion(4, 10, encryptionKey, header.session_password); 
	}
	else
		*encryptionKey = 0;

	free(hdr_block);
	indx_array = calloc(header.number_of_index_entries, sizeof(INDEX_DATA));
	fseek(fp, header.index_data_offset, SEEK_SET);
	num = fread((void*)indx_array, sizeof(INDEX_DATA), header.number_of_index_entries, fp);
	if (num != header.number_of_index_entries) {
		fprintf(stderr, "Can not read block index array\n");
		free(indx_array);
		return Py_BuildValue("i", 1);
	}
	
	diff_buffer = (si1 *)malloc(header.maximum_block_length * 4);
	in_data = malloc(inDataLength);
	outDataLength = blocks_per_cycle * header.maximum_block_length; //Note: this is only the max data length per decompression cycle....
	//data = calloc(numEntries, sizeof(ui4));	
	data = calloc(outDataLength, sizeof(ui4));
	if (data == NULL || in_data == NULL || diff_buffer == NULL) {
		fprintf(stderr, "malloc error\n");
		return Py_BuildValue("i", 1);
	}

	//Assemble output filename
	l = (int)strlen(fpath);
	memcpy(path, fpath, l-4);
	path[l-4] = '\0';

	
	fprintf(stdout, "\n\nReading file %s \n", fpath);
	start_block = 0;

	fprintf(stdout, "\nDecompressing and writing file %s: %ld entries \n", outFileName, header.number_of_samples);
	fprintf(stdout,"Num blocks%d\n",numBlocks);
	fprintf(stdout,"Num discontinuity %lu\n",header.number_of_discontinuity_entries);
	fprintf(stdout,"Sample rate: %f\n",header.sampling_frequency);
	fprintf(stdout,"Voltage conversion factor: %f\n",header.voltage_conversion_factor);
	while( start_block < numBlocks ) {
		end_block = start_block + blocks_per_cycle;
		if (end_block > numBlocks) {
			end_block = numBlocks;
			inDataLength = header.index_data_offset - indx_array[start_block].file_offset;
		}
		else {
			inDataLength = indx_array[end_block].file_offset - indx_array[start_block].file_offset;
		}
		
		fseek(fp, indx_array[start_block].file_offset, SEEK_SET);
		num = fread(in_data, 1, inDataLength, fp);
		if (num != inDataLength) {
			fprintf(stderr, "Data read error \n");
			return Py_BuildValue("i", 1);
		}
		
		dp = data;	idp = in_data;	dbp = diff_buffer;
		entryCounter = 0;
		for (i=start_block; i<end_block; i++)
		{
			bytesDecoded = RED_decompress_block(idp, dp, dbp, encryptionKey, 0, header.data_encryption_used, &RED_bk_hdr);

			// If current block is discontinuity, write out a file with previous 
			// block's timestamp and current data, reset num points
			if (start_block == 0) {
				previous_block_start_time = RED_bk_hdr.block_start_time;
			}
			if (RED_bk_hdr.discontinuity & (start_block != 0)) {
				fprintf(stdout,"discontinuity detected\n");
				end_block = start_block+i;
				previous_block_start_time = RED_bk_hdr.block_start_time;
				break;
			}

			idp += bytesDecoded;
			dp += RED_bk_hdr.sample_count;
			dbp = diff_buffer; //don't need to save diff_buffer- write over
			entryCounter += RED_bk_hdr.sample_count;
		}
		start_block = end_block;
		//open output file for writing
		fprintf(stdout,"End of cycle detected, writing block, size %lu \n",entryCounter);
		sprintf(outFileName, "%s-%lu-%s.raw32", path,previous_block_start_time,suffix);
		out_fp = fopen(outFileName, "w");
		if (out_fp == NULL) {
			fprintf(stderr, "Error opening file %s\n", outFileName);
			return Py_BuildValue("i", 1);
		}
		num = fwrite(data, sizeof(si4), entryCounter, out_fp);
		fclose(out_fp);

	} //end while()
	
	free(in_data); in_data = NULL;
	free(diff_buffer); diff_buffer = NULL;
	fprintf(stdout, "Decompression complete\n");
	fclose(fp);

	free(data); data = NULL;
	return Py_BuildValue("i", 0);
}
	/*
	Usage: mef2raw('filename.mef')
	Converts mef to raw 32 bit integers and saves to current directory
	Divides into multiple blocks
	*/


static PyObject* read_mef_header(PyObject *self, PyObject *args) {
	/*
	Usage: read_mef_header('filename.mef')
	Output:
	1. ch_name,
	2. num_samples,
	3. rec_start_time,
	4. rec_end_time,
	5. sampling_frequency,
	6. voltage_conversion_factor
	*/
	si4 num;
	char password[32];
	long unsigned num_samples;
	long unsigned rec_start_time;
	long unsigned rec_end_time;
	si4 voltage_conversion_factor;
	float sampling_frequency;
	si1 *ch_name;

	ui1 *bk_hdr;
	FILE *fp;
	MEF_HEADER_INFO *header;

	char *fpath;
	PyArg_ParseTuple(args, "s", &fpath);
	
	/*if (argc < 2 || argc > 3) 
	{
		(void) printf("USAGE: %s file_name [password] \n", argv[0]);
		return(1);
	}

	*password = 0;
	
	if (argc > 2)
	{
		//check password length
		if (strlen(argv[2]) > 16) {
			fprintf(stderr, "Error: Password cannot exceed 16 characters\n");
			return 1;
		}
		strcpy(password, argv[2]);
	}*/

	header = malloc(sizeof(MEF_HEADER_INFO)); memset(header, 0, sizeof(MEF_HEADER_INFO));
	
	fp = fopen(fpath, "r");
	if (fp == NULL) {
			fprintf(stderr, "Error opening file %s\n", fpath);
			return Py_BuildValue("i", 1);
		}		
	
	bk_hdr = calloc(sizeof(ui1), MEF_HEADER_LENGTH);	
	num = fread(bk_hdr, 1, MEF_HEADER_LENGTH, fp);
	if (num != MEF_HEADER_LENGTH) {
		fprintf(stderr, "Error reading file %s\n", fpath);
		return Py_BuildValue("i", 1);
	}
	
	(void)read_mef_header_block(bk_hdr, header, password);
	showHeader(header);
		
	ch_name = header->channel_name;
	num_samples = header->number_of_samples;
	rec_start_time = header->recording_start_time;
	rec_end_time = header->recording_end_time;
	sampling_frequency = header->sampling_frequency;
	voltage_conversion_factor = header->voltage_conversion_factor;

	printf("Done Showing hdr\n");
	free(bk_hdr); bk_hdr = NULL;

	return Py_BuildValue("slllfdl",ch_name,num_samples,rec_start_time,rec_end_time,sampling_frequency,voltage_conversion_factor);
}
/* 
 * Bind Python fn names to c fns
 */

static PyMethodDef mef2raw_methods[] = {
	{"convert_to_raw",mef_to_raw,METH_VARARGS,"Convert Mef To Raw 32 bit ints"},
	{"read_header",read_mef_header,METH_VARARGS,"Read Mef Header"},
	{NULL, NULL}
};

/*
 * Python calls this to let us initialize our module
 */
void initpymef2()
{
	PyImport_AddModule("pymef2");
	Py_InitModule("pymef2", mef2raw_methods);
}
