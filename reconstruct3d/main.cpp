#include <iostream>
#include <fstream>
#include <opencv2\core\core.hpp>
#include <opencv2\highgui\highgui.hpp>
#include "getopt.h"
#include "ply.h"

// macro: handling return situation
#define ERROR		(1)
#define SUCCESS		(0)

#ifdef _DEBUG
#define RETURN(x)	{system("pause"); return x;}
#else
#define RETURN(x)	{return x;}
#endif // _DEBUG

//// macro: accelerate accessing cv::mat<float>
//#define AT_F(A, i)		(((float*)A.data)[(i)])
//#define AT_F2D(A, i, j)	(((float*)A.data)[(i)*(A.cols)+(j)])

// namespace utilize
using namespace std;
using namespace cv;

// prototypes
void plot(string name, InputArray& matrix);
void calcualte_normal(vector<pair<Vec3f, Mat>>& data, OutputArray normal, OutputArray albedo);
void calculate_depth(InputArray normal, OutputArray depth);

/*
 * main
 *
 * arguments:
 *  [OPTION] -o [NAME]	output filename
 *  [OPTION] -s			enable show result
 *  [REQUIRE]	[NAME]	input directory
 */
int main(const int argc, char* const argv[]) {
	// parameters
	string folder_input;
	string filename_output = "result.ply";
	bool should_show_result = false;

	// parsing arguments
	int c;
	while ((c = getopt(argc, argv, "o:s")) != -1) {
		switch (c) {
		case 'o':
			filename_output = optarg;
			break;
		case 's':
			should_show_result = true;
			break;
		}
	}

	if (optind >= argc) {
		cerr << "input soucre not specific" << endl;
		RETURN(ERROR);
	}
	else {
		folder_input = argv[optind];
	}

	// read data
	vector<pair<Vec3f, Mat>> data;

	// read light source file
	ifstream file_light(folder_input + "\\LightSource.txt");
	if (!file_light.is_open()) {
		cerr << "light source file not found" << endl;
		RETURN(ERROR);
	}


	string line;
	while (getline(file_light, line)) {
		// parsing light source file
		int i, pt_x, pt_y, pt_z;
		sscanf_s(line.c_str(), "pic%d: (%d,%d,%d)", &i, &pt_x, &pt_y, &pt_z);

		// read image
		Mat mat;

		string fn(folder_input + "\\pic" + to_string(i) + ".bmp");
		imread(fn, IMREAD_GRAYSCALE).convertTo(mat, CV_32FC1);

		// check image loaded
		if (mat.empty()) {
			cerr << "image not found: " << fn << endl;
			RETURN(ERROR);
		}

		// save
		data.push_back(make_pair(Vec3f(pt_x, pt_y, pt_z), mat));
	}

	// calculate normal
	Mat normal, albedo;
	calcualte_normal(data, normal, albedo);

	if (should_show_result) {
		plot("albedo", albedo);
	}

	// calculate depth
	Mat depth;
	calculate_depth(normal, depth);

	if (should_show_result) {
		plot("depth", depth);
	}

	// return
	if (should_show_result) {
		clog << "process finished. press any key to continue." << endl;
		waitKey();
	}

	RETURN(SUCCESS);
}

/*
 * assisting function for imshow
 * this func normalized the image before display
 */
void plot(string name, InputArray& matrix) {
	Mat A = matrix.getMat();

	double min_val, max_val;
	minMaxLoc(A, &min_val, &max_val);
	imshow(name, (A - min_val) / (max_val - min_val));
}

/*
 *	calcuate normal map and albedo
 */
void calcualte_normal(vector<pair<Vec3f, Mat>>& data, OutputArray _normal, OutputArray _albedo) {
	const auto n = data.size();
	const auto size = data[0].second.size();

	// build light source vector S
	Mat S(n, 3, CV_32FC1);
	for (int i = 0; i < n; i++) {
		S.row(i) = data[i].first;
	}

	Mat S_sol;
	invert(S, S_sol, DECOMP_SVD);

	// build normal map
	_normal.create(size, CV_32FC3);
	_normal.setTo(0);

	_albedo.create(size, CV_32FC1);
	_albedo.setTo(0);

	auto normal = _normal.getMat();
	auto albedo = _albedo.getMat();

	const auto eps = numeric_limits<double>::epsilon();
	for (int i = 0, r_idx = 0; i < size.height; i++, r_idx += size.width) {
		for (int j = 0; j < size.width; j++) {
			Mat I(n, 1, CV_32FC1);
			for (int k = 0; k < n; k++) {
				I.at<float>(k) = data[k].second.at<float>(r_idx + j);
			}

			Mat b = S_sol *I;
			auto albedo_val = norm(b, NORM_L2);

			if (albedo_val > eps) { // floating point inaccuracy
				normal.at<Vec3f>(i, j) = Mat(b / albedo_val);	// N = b / |b|
				albedo.at<float>(r_idx + j) = albedo_val;			// A = |b|
			}
		}
	}
}

/*
 * surface reconstruct from normal vectors
 */
void calculate_depth(InputArray _normal, OutputArray _depth) {
	// split channels
	vector<Mat> normals;
	split(_normal, normals);

	// diff
	Mat n2, n1;
	divide(normals[0], normals[2], n2); // df/dx, aka `u`
	divide(normals[1], normals[2], n1); // df/dy, aka `v`

	n2 = -n2;
	n1 = -n1;

	//// sanity check
	//const int trim_row = n1.rows - 1;
	//const int trim_col = n2.cols - 1;
	//auto z_dx_dy = n2(Rect(0, 1, trim_col, trim_row)) - n2(Rect(0, 0, trim_col, trim_row));
	//auto z_dy_dx = n1(Rect(1, 0, trim_col, trim_row)) - n1(Rect(0, 0, trim_col, trim_row));

	//plot("sanity", z_dx_dy - z_dy_dx);

	// integral for depths
	const auto ctr_h = n1.rows >> 1;
	const auto ctr_w = n2.cols >> 1;


	for (int i = 1; i < n1.rows; i++) {
		for (int j = 1; j < n1.cols; j++) {
			n1.at<float>(i, j) += n1.at<float>(i - 1, j);
			n2.at<float>(i, j) += n2.at<float>(i, j - 1);
		}
	}

	Mat depth = n1 + n2;
	depth.copyTo(_depth);
}