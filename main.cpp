#include "config.h"
#include "numtype.h"

#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <vector>


#include "rng.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "vector.hpp"
#include "matrix.hpp"
#include "camera.hpp"

#include "primitive.hpp"
#include "bilinear.hpp"
#include "sphere.hpp"

#include "light.hpp"
#include "point_light.hpp"


#include "config.hpp"

//#include <OSL/oslexec.h>

#include <boost/program_options.hpp>
namespace BPO = boost::program_options;

#define THREADS 1
#define SPP 1
#define XRES 512
#define YRES 288
#define NUM_RAND_PATCHES 1000
#define NUM_RAND_SPHERES 1000
#define FRAC_MB 0.1
#define CAMERA_SPIN 10.0
#define LENS_DIAM 1.0


// Holds a pair of integers
struct IntPair {
	int a;
	int b;

	IntPair() {
		a = 0;
		b = 0;
	}

	IntPair(int i1, int i2) {
		a = i1;
		b = i2;
	}
};

// Called by program_options to parse a set of IntPair arguments
void validate(boost::any& v, const std::vector<std::string>& values,
              IntPair*, int)
{
	IntPair intpair;

	//Extract tokens from values string vector and populate IntPair struct.
	if (values.size() < 2) {
		throw BPO::validation_error(BPO::validation_error::invalid_option_value,
		                            "Invalid IntPair specification, requires two ints");
	}

	intpair.a = boost::lexical_cast<int>(values.at(0));
	intpair.b = boost::lexical_cast<int>(values.at(1));

	v = intpair;
}


int main(int argc, char **argv)
{
	RNG rng(42);
	RNG rng2(865546);

	/*
	 **********************************************************************
	 * Print program information
	 **********************************************************************
	 */
	std::cout << "Psychopath v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH;
#ifdef DEBUG
	std::cout << " (DEBUG build)";
#endif
	std::cout << std::endl << std::endl;

#ifdef DEBUG
	std::cout << std::endl << "Struct sizes:" << std::endl;
	std::cout << "Ray: " << sizeof(Ray) << std::endl;
	std::cout << "BBounds: " << sizeof(BBox) << std::endl;
	std::cout << "BBox: " << sizeof(BBoxT) << std::endl;
	std::cout << "BVHNode: " << sizeof(BVHNode) << std::endl;
	std::cout << "Grid: " << sizeof(Grid) << std::endl;
	std::cout << "GridBVHNode: " << sizeof(GridBVHNode) << std::endl;
	std::cout << "Primitive *: " << sizeof(Primitive *) << std::endl;
	std::cout << "TimeBox<int32>: " << sizeof(std::vector<int32>) << std::endl;
	std::cout << "std::vector<int32>: " << sizeof(std::vector<int32>) << std::endl;
#endif


	/*
	 **********************************************************************
	 * Command-line options.
	 **********************************************************************
	 */
	int spp = SPP;
	int threads = THREADS;
	std::string output_path = "test.png";
	IntPair resolution(XRES, YRES);

	// Define them
	BPO::options_description desc("Allowed options");
	desc.add_options()
	("help,h", "Print this help message")
	("spp", BPO::value<int>(), "Number of samples to take per pixel")
	("threads,t", BPO::value<int>(), "Number of threads to render with")
	("output,o", BPO::value<std::string>(), "The PNG file to render to")
	("resolution,r", BPO::value<IntPair>()->multitoken(), "The resolution to render at, e.g. 1280 720")
	;

	// Collect them
	BPO::variables_map vm;
	BPO::store(BPO::parse_command_line(argc, argv, desc), vm);
	BPO::notify(vm);

	// Help message
	if (vm.count("help")) {
		std::cout << desc << "\n";
		return 1;
	}

	// Samples per pixel
	if (vm.count("spp")) {
		spp = vm["spp"].as<int>();
		if (spp < 1)
			spp = 1;
		std::cout << "Samples per pixel: " << spp << "\n";
	}

	// Thread count
	if (vm.count("threads")) {
		threads = vm["threads"].as<int>();
		if (threads < 1)
			threads = 1;
		std::cout << "Threads: " << threads << "\n";
	}

	// Output file
	if (vm.count("output")) {
		output_path = vm["output"].as<std::string>();
		std::cout << "Output path: " << output_path << "\n";
	}

	// Resolution
	if (vm.count("resolution")) {
		resolution = vm["resolution"].as<IntPair>();
		std::cout << "Resolution: " << resolution.a << " " << resolution.b << "\n";
	}


	/*
	 **********************************************************************
	 * Build scene
	 **********************************************************************
	 */
	Scene scene;

	// Add camera
	std::vector<Matrix44> cam_mats;
	cam_mats.resize(4);

	float angle = CAMERA_SPIN * (3.14159 / 180.0);
	Vec3 axis(0.0, 0.0, 1.0);

	cam_mats[0].translate(Vec3(0.0, 0.0, -40.0));
	cam_mats[0].rotate(0.0, axis);
	cam_mats[0].translate(Vec3(0.0, 0.0, 20.0));

	cam_mats[1].translate(Vec3(0.0, 0.0, -40.0));
	cam_mats[1].rotate((angle/3)*1, axis);
	cam_mats[1].translate(Vec3(0.0, 0.0, 20.0));

	cam_mats[2].translate(Vec3(0.0, 0.0, -40.0));
	cam_mats[2].rotate((angle/3)*2, axis);
	cam_mats[2].translate(Vec3(0.0, 0.0, 20.0));

	cam_mats[3].translate(Vec3(0.0, 0.0, -40.0));
	cam_mats[3].rotate((angle/3)*3, axis);
	cam_mats[3].translate(Vec3(0.0, 0.0, 20.0));

#define FOCUS_DISTANCE 40.0
#define FOV 55
	scene.camera = new Camera(cam_mats, (3.14159/180.0)*FOV, LENS_DIAM, FOCUS_DISTANCE);


	// Add lights
	PointLight *pl = new PointLight(Vec3(10.0, 10.0, -10.0),
	                                Color(200.0));
	scene.add_finite_light(pl);


	// Add random patches
	std::cout << "Generating random patches...";
	std::cout.flush();

	Bilinear *patch;
	for (int i=0; i < NUM_RAND_PATCHES; i++) {
		float32 x, y, z;
		z = 15 + (rng.next_float() * NUM_RAND_PATCHES / 4);
		float32 s = z / 15;
		x = rng.next_float_c() * 40;
		y = rng.next_float_c() * 20;

		// Motion?
		int ms = 1;
		if (rng.next_float() < FRAC_MB)
			ms = 2;

		// Flipped?
		bool flip = true;
		//if (rng.next_float() < 0.5)
		//	flip = false;

		patch = new Bilinear(ms);

		for (int j = 0; j < ms; j++) {
			x += (rng.next_float_c() * j * 4) / s;
			y += (rng.next_float_c() * j * 4) / s;
			z += (rng.next_float_c() * j * 4) / s;

			if (flip) {
				patch->add_time_sample(j,
				                       Vec3((x*s)+2, (y*s)+2, z+(rng.next_float_c()*2)),
				                       Vec3((x*s)+2, (y*s)-2, z+(rng.next_float_c()*2)),
				                       Vec3((x*s)-2, (y*s)-2, z+(rng.next_float_c()*2)),
				                       Vec3((x*s)-2, (y*s)+2, z+(rng.next_float_c()*2)));
			} else {
				patch->add_time_sample(j,
				                       Vec3((x*s)+2, (y*s)+2, z+(rng.next_float_c()*2)),
				                       Vec3((x*s)-2, (y*s)+2, z+(rng.next_float_c()*2)),
				                       Vec3((x*s)-2, (y*s)-2, z+(rng.next_float_c()*2)),
				                       Vec3((x*s)+2, (y*s)-2, z+(rng.next_float_c()*2)));

			}
		}

		scene.add_primitive(patch);
	}
	std::cout << " done." << std::endl;
	std::cout.flush();


	// Add random spheres
	std::cout << "Generating random spheres...";
	std::cout.flush();

	const float32 radius = 0.2;

	Sphere *sphere;
	for (int i=0; i < NUM_RAND_SPHERES; i++) {
		float32 x, y, z;
		z = 15 + (rng2.next_float() * NUM_RAND_SPHERES / 4) * radius;
		float32 s = z / 15;
		x = rng2.next_float_c() * 40;
		y = rng2.next_float_c() * 20;

		// Motion?
		int ms = 1;
		if (rng2.next_float() < FRAC_MB)
			ms = 2;

		sphere = new Sphere(ms);

		for (int j = 0; j < ms; j++) {
			x += (rng2.next_float_c() * j * 4) / s;
			y += (rng2.next_float_c() * j * 4) / s;
			z += (rng2.next_float_c() * j * 4) / s;

			sphere->add_time_sample(j,
			                        Vec3(x*s, y*s, z+(rng2.next_float_c()*2)),
			                        1.0f);
		}

		scene.add_primitive(sphere);
	}
	std::cout << " done." << std::endl;
	std::cout.flush();

	std::cout << "Finalizing scene... ";
	std::cout.flush();
	scene.finalize();
	std::cout << " done." << std::endl;
	std::cout.flush();


	/*
	 **********************************************************************
	 * Generate image
	 **********************************************************************
	 */

	std::cout << "\nStarting render: \n";
	std::cout.flush();
	Renderer r(&scene, resolution.a, resolution.b, spp, output_path);
	r.render(threads);

	return 0;
}
