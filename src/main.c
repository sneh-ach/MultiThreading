// MIT License
//
// Copyright (c) 2023 Trevor Bakker
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <stdint.h>
#include <pthread.h>
#include "utility.h"
#include "star.h"
#include "float.h"

#define NUM_STARS 30000
#define MAX_LINE 1024
#define DELIMITER " \t\n"

// ThreadData struct contains the start and end indices of the array to be processed by a thread
typedef struct
{
  int start;
  int end;
  struct Star *arr;
  double sum;     // Add sum to the struct
  uint64_t count; // Add count to the struct
} ThreadData;

struct Star star_array[NUM_STARS]; // Array to store star data

double min = DBL_MAX; // Initialize min distance as maximum possible double value
double max = 0;       // Initialize max distance as 0
double mean = 0;      // Initialize mean distance as 0

pthread_mutex_t min_max_mean_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to synchronize access to min, max, and mean

void showHelp()
{
  printf("Use: findAngular [options]\n");
  printf("Where options are:\n");
  printf("-t          Number of threads to use\n");
  printf("-h          Show this help\n");
}

// Function to be executed by each thread
// This function calculates the angular distance between each pair of stars
// within the given range and updates the min, max, and mean distances.
void *determineAverageAngularDistanceThread(void *arg)
{
  ThreadData *data = (ThreadData *)arg;
  int start = data->start;
  int end = data->end;
  struct Star *arr = data->arr;

  uint32_t i, j;
  uint64_t count = 0;
  double sum = 0;
  double local_min = DBL_MAX;
  double local_max = 0;

  for (i = start; i < end; i++)
  {
    for (j = i + 1; j < NUM_STARS; j++) // Start j from i+1, no need for distance_calculated matrix
    {
      double distance = calculateAngularDistance(arr[i].RightAscension, arr[i].Declination,
                                                 arr[j].RightAscension, arr[j].Declination);

      // Update the local min and max distances
      count++;
      if (local_min > distance)
        local_min = distance;
      if (local_max < distance)
        local_max = distance;
    }
  }
  data->sum = sum;
  data->count = count;

  // Lock the mutex to update the global min, max, and mean
  pthread_mutex_lock(&min_max_mean_mutex);

  if (min > local_min)
    min = local_min;
  if (max < local_max)
    max = local_max;

  pthread_mutex_unlock(&min_max_mean_mutex);

  return NULL;
}

// Function to create threads and calculate the average angular distance
// This function divides the work of calculating the angular distance between
// star pairs among the specified number of threads and waits for them to finish.
void determineAverageAngularDistance(struct Star *arr, int num_threads)
{
  // Create an array of pthread_t to store thread handles
  pthread_t threads[num_threads];
  // Create an array of ThreadData to store the data for each thread
  ThreadData data[num_threads];

  // Calculate the number of stars each thread should process
  int stars_per_thread = NUM_STARS / num_threads;

  // Create threads and assign them their respective range of stars
  for (int i = 0; i < num_threads; i++)
  {
    data[i].start = i * stars_per_thread;
    data[i].end = (i == num_threads - 1) ? NUM_STARS : (i + 1) * stars_per_thread;
    data[i].arr = arr;
    pthread_create(&threads[i], NULL, determineAverageAngularDistanceThread, (void *)&data[i]);
  }

  // Wait for all threads to finish
  for (int i = 0; i < num_threads; i++)
  {
    pthread_join(threads[i], NULL);
  }

  // Accumulate the sums and counts from each thread
  double total_sum = 0;
  uint64_t total_count = 0;
  for (int i = 0; i < num_threads; i++)
  {
    total_sum += data[i].sum;
    total_count += data[i].count;
  }

  // Calculate the overall mean
  mean = total_sum / total_count;
}

int main(int argc, char *argv[])
{
  FILE *fp;
  uint32_t star_count = 0;
  int num_threads = 1;
  int opt;

  // Parse command line options
  // This allows the user to specify options like the number of threads to use or display help
  while ((opt = getopt(argc, argv, "t:h")) != -1)
  {
    switch (opt)
    {
    case 't':
      num_threads = atoi(optarg); // Assign the number of threads specified by the user
      break;
    case 'h':
      showHelp(); // Show the help message and exit the program
      exit(0);
    default: // If an invalid option is provided, show the help message and exit the program
      showHelp();
      exit(1);
    }
  }

  // Initialize the mutex used to synchronize access to min, max, and mean
  if (pthread_mutex_init(&min_max_mean_mutex, NULL) != 0)
  {
    printf("Mutex initialization failed\n");
    exit(1);
  }

  // Open the input file containing star data
  // This file contains information about the stars, such as ID, Right Ascension, and Declination
  fp = fopen("data/tycho-trimmed.csv", "r");

  if (fp == NULL)
  {
    printf("ERROR: Unable to open the file data/tycho-trimmed.csv\n");
    exit(1);
  }

  // Read the input file line by line and parse the star data
  // Store the star data in the star_array
  char line[MAX_LINE];
  while (fgets(line, 1024, fp))
  {
    uint32_t column = 0;

    char *tok;
    for (tok = strtok(line, " ");
         tok && *tok;
         tok = strtok(NULL, " "))
    {
      switch (column)
      {
      case 0:
        star_array[star_count].ID = atoi(tok); // Store the star ID
        break;

      case 1:
        star_array[star_count].RightAscension = atof(tok); // Store the star Right Ascension
        break;

      case 2:
        star_array[star_count].Declination = atof(tok); // Store the star Declination
        break;

      default:
        printf("ERROR: line %d had more than 3 columns\n", star_count);
        exit(1);
        break;
      }
      column++;
    }
    star_count++;
  }
  fclose(fp); // Close the input file after reading all the star data

  printf("%d records read\n", star_count);

  // Calculate the average angular distance using the specified number of threads
  clock_t start_time = clock(); // Record the start time
  determineAverageAngularDistance(star_array, num_threads);
  clock_t end_time = clock();                                               // Record the end time
  double elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC; // Calculate elapsed time in seconds

  // Print the results
  printf("Average distance found is %lf\n", mean);
  printf("Minimum distance found is %lf\n", min);
  printf("Maximum distance found is %lf\n", max);
  printf("Time taken to compute angular distances: %lf seconds\n", elapsed_time);

  return 0;
}