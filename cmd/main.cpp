#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <assert.h>
#include <string>
#include <ctime>

//> Inluce OpenMP here
#include <omp.h>

//> Eigen library
#include <Eigen/Core>
#include <Eigen/Dense>

//> Include functions

#include "../Edge_Reconst/util.hpp"
#include "../Edge_Reconst/PairEdgeHypo.hpp"
#include "../Edge_Reconst/getReprojectedEdgel.hpp"
#include "../Edge_Reconst/getQuadrilateral.hpp"
#include "../Edge_Reconst/getSupportedEdgels.hpp"
#include "../Edge_Reconst/getOrientationList.hpp"
#include "../Edge_Reconst/linearTriangulationUtil.hpp"
#include "../Edge_Reconst/definitions.h"

//> Added by CH: Efficient Bucketing Method
#include "../Edge_Reconst/lemsvpe_CH/vgl_polygon_CH.hpp"
#include "../Edge_Reconst/lemsvpe_CH/vgl_polygon_scan_iterator_CH.hpp"
#include "../Edge_Reconst/subpixel_point_set.hpp"

// using namespace std;
using namespace MultiviewGeometryUtil;

// ========================================================================================================================
// main function
//
// Modifications
//    Chiang-Heng Chien  23-07-18    Initially create a blank repository with minor multiview geometry utility functions.
//    Chiang-Heng Chien  23-08-19    Add edited bucketing method from VXL. If this repo is to be integrated under LEMSVPE 
//                                   scheme, simply use vgl library from VXL. This is very easy to be handled.
//    Chiang-Heng Chien  23-08-27    Add bucket building and fetching edgels from bucket coordinate code for efficient edgel
//                                   accessing from buckets inside a given quadrilateral.
//
//> (c) LEMS, Brown University
//> Yilin Zheng (yilin_zheng@brown.edu)
//> Chiang-Heng Chien (chiang-heng_chien@brown.edu)
// =========================================================================================================================

void getInteriorBuckets(
  const vgl_polygon_CH<double> &p, bool boundary_in, 
  std::vector<Eigen::Vector2d> &InteriorBucketCoordinates
)
{
  // iterate
  vgl_polygon_scan_iterator_CH<double> it(p, boundary_in); 

  //std::cout << "Interior points:\n";
  for (it.reset(); it.next(); ) {
    int y = it.scany();
    for (int x = it.startx(); x <= it.endx(); ++x) {
      //std::cout << "(" << x << "," << y << ") ";
      Eigen::Vector2d Bucket_Coordinate;
      Bucket_Coordinate << x, y;
      InteriorBucketCoordinates.push_back( Bucket_Coordinate );
    }
  }
  //std::cout << std::endl;
}

void getEdgelsFromInteriorQuadrilateral( 
  const subpixel_point_set &sp_pts, 
  const std::vector<Eigen::Vector2d> &InteriorBucketCoordinates,
  std::vector< unsigned > &Edgel_Indices 
)
{
  //> Traverse all buckets inside the quadrilateral
  //std::cout << "Number of interior bucket coordinates: " << InteriorBucketCoordinates.size()<<std::endl;
  //std::cout<<"bucket coordinates(starts from 0) are shown below: "<<std::endl;
  for (int bi = 0; bi < InteriorBucketCoordinates.size(); bi++) {
    
    unsigned const i_col = InteriorBucketCoordinates[bi](0);  //> x
    unsigned const i_row = InteriorBucketCoordinates[bi](1);  //> y

    //std::cout<< "coordinate " << bi << ": "<< i_col<< ", "<< i_row <<std::endl;
    //std::cout<< i_col << ", "<< i_row << ";" <<std::endl;


    //> Ignore if bucket coordinate exceeds image boundary
    if (i_row >= sp_pts.nrows() || i_col >= sp_pts.ncols()) continue;

    //> Traverse all edgels inside the bucket
    for (unsigned k = 0; k < sp_pts.cells()[i_row][i_col].size(); ++k) {
      unsigned const p2_idx = sp_pts.cells()[i_row][i_col][k];
      //std::cout<< "inlier edge index(starts from 0): " << p2_idx <<std::endl;
      Edgel_Indices.push_back(p2_idx);
    }
    //std::cout << "sp_pts.cells()[i_row][i_col].size(): " << sp_pts.cells()[i_row][i_col].size() <<std::endl;
  }
  
  //std::cout << "number of edges in this quadrilateral found by bucketing: "<< Edgel_Indices.size() <<std::endl;
}

int main(int argc, char **argv) {
  std::fstream Edge_File;
  std::fstream Edge_File_H12;
  std::fstream Rmatrix_File;
  std::fstream Tmatrix_File;
  std::fstream Kmatrix_File;
  std::fstream H1remove_File;
  std::fstream H2remove_File;
  int d = 0;
  int q = 0;
  int file_idx = 1;
  double rd_data;
  std::vector<Eigen::MatrixXd> All_Edgels; 
  std::vector<Eigen::MatrixXd> All_Edgels_H12;  
  //Eigen::MatrixXd Edgels;
  Eigen::Vector4d row_edge;

  //> All_Bucketed_Imgs stores all "bucketed" images
  //> (A "bucketed image" means that edgels are inserted to the buckets of that image)
  std::vector< subpixel_point_set > All_Bucketed_Imgs;
  std::cout << "read edges file now\n";
  while(file_idx < DATASET_NUM_OF_FRAMES+1) {
    // std::string Edge_File_Path = REPO_DIR + "datasets/T-Less/10/Edges/thresh/Edge_"+std::to_string(file_idx)+"_t"+std::to_string(threshEDGforall)+".txt"; 
    std::string Edge_File_Path = REPO_DIR + "datasets/ABC-NEF/00000006/Edges/Edge_"+std::to_string(file_idx)+"_t"+std::to_string(threshEDGforall)+".txt";
    file_idx ++;
    Eigen::MatrixXd Edgels; //> Declare locally, ensuring the memory addresses are different for different frames
    Edge_File.open(Edge_File_Path, std::ios_base::in);
    if (!Edge_File) { 
       std::cerr << "Edge file not existed!\n"; exit(1); 
    }
    else {
      Edgels.resize(1,4);
      while (Edge_File >> rd_data) {
        row_edge(q) = rd_data;
        q++;
        if(q>3){
          Edgels.conservativeResize(d+1,4);
          Edgels.row(d) = row_edge;
          q = 0;
          d++;
        }
      }
      Edge_File.close();
      All_Edgels.push_back(Edgels);

      //> Building bucket grid for the current frame
      subpixel_point_set bucketed_img( Edgels );
      bucketed_img.build_bucketing_grid( imgrows, imgcols );
      All_Bucketed_Imgs.push_back( bucketed_img );

      //Edgels = {};
      d = 0;
      q = 0;
    }
  }
  
  std::cout<< "Edge file loading finished" <<std::endl;

  file_idx = 1;
  int H_idx = 0;
  while(file_idx < 3) {
    if(file_idx == 1){
      H_idx = HYPO1_VIEW_INDX;
    }else{
      H_idx = HYPO2_VIEW_INDX;
    }
    // std::string Edge_File_PathH12 = REPO_DIR + "datasets/T-Less/10/Edges/thresh/Edge_"+std::to_string(H_idx+1)+"_t"+std::to_string(threshEDG)+".txt"; 
    std::string Edge_File_PathH12 = REPO_DIR + "datasets/ABC-NEF/00000006/Edges/Edge_"+std::to_string(H_idx+1)+"_t"+std::to_string(threshEDG)+".txt"; 
    
    file_idx ++;
    Eigen::MatrixXd Edgels; //> Declare locally, ensuring the memory addresses are different for different frames
    Edge_File.open(Edge_File_PathH12, std::ios_base::in);
    if (!Edge_File) { 
       std::cerr << "Edge file not existed!\n"; exit(1); 
    }
    else {
      Edgels.resize(1,4);
      while (Edge_File >> rd_data) {
        row_edge(q) = rd_data;
        q++;
        if(q>3){
          Edgels.conservativeResize(d+1,4);
          Edgels.row(d) = row_edge;
          q = 0;
          d++;
        }
      }
      Edge_File.close();
      All_Edgels_H12.push_back(Edgels);
      //Edgels = {};
      d = 0;
      q = 0;
    }
  }
  file_idx = 1;
  std::cout<< "HYPO1 and HYPO2 Edge file loading finished" <<std::endl;

  std::vector<Eigen::Matrix3d> All_R;
  Eigen::Matrix3d R_matrix;
  Eigen::Vector3d row_R;
  std::string Rmatrix_File_Path = REPO_DIR + "datasets/ABC-NEF/00000006/RnT/R_matrix.txt";
  Rmatrix_File.open(Rmatrix_File_Path, std::ios_base::in);
  if (!Rmatrix_File) { 
    std::cerr << "R_matrix file not existed!\n"; exit(1); 
    }
  else {
    while (Rmatrix_File >> rd_data) {
      row_R(q) = rd_data;
      q++;
      if(q>2){
        R_matrix.row(d) = row_R;
        row_R = {};
        q = 0;
        d++;
      }
      if(d>2){
        All_R.push_back(R_matrix);
        R_matrix = {};
        d = 0;
      }
    }
    Rmatrix_File.close();
  }

  std::cout<< "R matrix loading finished" <<std::endl;

  std::vector<Eigen::Vector3d> All_T;
  Eigen::Vector3d T_matrix;
  std::string Tmatrix_File_Path = REPO_DIR + "datasets/ABC-NEF/00000006/RnT/T_matrix.txt";
  Tmatrix_File.open(Tmatrix_File_Path, std::ios_base::in);
  if (!Tmatrix_File) { 
    std::cerr << "T_matrix file not existed!\n"; exit(1); 
    }
  else {
    while (Tmatrix_File >> rd_data) {
      T_matrix(d) = rd_data;
      d++;
      if(d>2){
        All_T.push_back(T_matrix);
        T_matrix = {};
        d = 0;
      }
    }
    Tmatrix_File.close();
  }
  
  std::cout<< "T matrix loading finished" <<std::endl;

  Eigen::Matrix3d K;
  std::vector<Eigen::Matrix3d> All_K;
  if(IF_MULTIPLE_K == 1){
    Eigen::Matrix3d K_matrix;
    Eigen::Vector3d row_K;
    std::string Kmatrix_File_Path = REPO_DIR + "datasets/ABC-NEF/00000006/RnT/K_matrix.txt";
    Kmatrix_File.open(Kmatrix_File_Path, std::ios_base::in);
    if (!Kmatrix_File) { 
      std::cerr << "K_matrix file not existed!\n"; exit(1);
    }else {
      while (Kmatrix_File >> rd_data) {
        row_K(q) = rd_data;
        q++;
        if(q>2){
          K_matrix.row(d) = row_K;
          row_K = {};
          q = 0;
          d++;
        }
        if(d>2){
          All_K.push_back(K_matrix);
          K_matrix = {};
          d = 0;
        }
      }
      Kmatrix_File.close();
    }
  }else{
    // Cabinet
    // K<< 537.960322000000, 0, 319.183641000000, 0,	539.597659000000,	247.053820000000,0,	0,	1;
    // ICL-NUIM_officekt1
    K<< 481.2000000000000, 0, 319.5000000000000, 0,	-480,	239.5000000000000,0,	0,	1;
  }

  std::cout<< "K matrix loading finished" <<std::endl;

  std::vector<int> edge_idx_H1;
  std::vector<int> edge_idx_H2;
  int removeH1idx;
  int removeH2idx;
  std::string H1remove_File_Path = REPO_DIR + "datasets/ABC-NEF/00000006/removeidx/H1remove_idx3.txt";
  std::string H2remove_File_Path = REPO_DIR + "datasets/ABC-NEF/00000006/removeidx/H2remove_idx3.txt";
  if(NOT1STROUND == 1){  
    H1remove_File.open(H1remove_File_Path, std::ios_base::in);
    if (!H1remove_File) {
      std::cerr << "H1remove file not existed!\n"; exit(1); 
    }else {
      while (H1remove_File >> rd_data) {
        removeH1idx = rd_data;
        edge_idx_H1.push_back(removeH1idx);
        }
    }
    H1remove_File.close();

    H2remove_File.open(H2remove_File_Path, std::ios_base::in);
    if (!H2remove_File) {
      std::cerr << "H2remove file not existed!\n"; exit(1); 
    }else {
      while (H2remove_File >> rd_data) {
        removeH2idx = rd_data;
        edge_idx_H2.push_back(removeH2idx);
        }
    }
    H2remove_File.close();

  std::cout<< "remove idx loading finished" <<std::endl;
  std::cout<< "H1 info: " << edge_idx_H1.size()<<std::endl;
  std::cout<< "H1: " << edge_idx_H1[0]<<" "<< edge_idx_H1[1]<<" "<< edge_idx_H1[2]<<std::endl;
  std::cout<< "H2 info: " << edge_idx_H2.size()<<std::endl;
  std::cout<< "H2: " << edge_idx_H2[0]<<" "<< edge_idx_H2[1]<<" "<< edge_idx_H2[2]<<std::endl;
  
  }



  //<<<<<<<<< Pipeline start >>>>>>>>>//
  clock_t tstart_pre, tend_pre;
  tstart_pre = clock();
  ////////PREPROCESSING/////////
  MultiviewGeometryUtil::multiview_geometry_util util;
  PairEdgeHypothesis::pair_edge_hypothesis       PairHypo;
  GetReprojectedEdgel::get_Reprojected_Edgel     getReprojEdgel;
  GetQuadrilateral::get_Quadrilateral            getQuad;
  GetSupportedEdgels::get_SupportedEdgels        getSupport;
  GetOrientationList::get_OrientationList        getOre;
  
  // Assign variables required for Hypo1 and Hypo2
  Eigen::MatrixXd Edges_HYPO1 = All_Edgels_H12[0];
  Eigen::MatrixXd Edges_pairs = All_Edgels[HYPO1_VIEW_INDX];
  Eigen::Matrix3d R1          = All_R[HYPO1_VIEW_INDX];
  Eigen::Vector3d T1          = All_T[HYPO1_VIEW_INDX];
  Eigen::MatrixXd Edges_HYPO2 = All_Edgels_H12[1];
  Eigen::Matrix3d R2          = All_R[HYPO2_VIEW_INDX];
  Eigen::Vector3d T2          = All_T[HYPO2_VIEW_INDX];
  Eigen::MatrixXd Edges_HYPO1_all = All_Edgels[HYPO1_VIEW_INDX];
  Eigen::MatrixXd Edges_HYPO2_all = All_Edgels[HYPO2_VIEW_INDX];
  // deal with multiple K scenario
  Eigen::Matrix3d K1;
  Eigen::Matrix3d K2;
  if(IF_MULTIPLE_K == 1){
    K1 = All_K[HYPO1_VIEW_INDX];
    K2 = All_K[HYPO2_VIEW_INDX];
  }else{
    K1 = K;
    K2 = K;
  }
  // Relative pose calculation
  Eigen::Matrix3d R21 = util.getRelativePose_R21(R1, R2);
  Eigen::Vector3d T21 = util.getRelativePose_T21(R1, R2, T1, T2);
  Eigen::Matrix3d Tx  = util.getSkewSymmetric(T21);
  Eigen::Matrix3d E   = util.getEssentialMatrix(R21, T21);
  Eigen::Matrix3d F   = util.getFundamentalMatrix(K1.inverse(), K2.inverse(), R21, T21);
  Eigen::Matrix3d R12 = util.getRelativePose_R21(R2, R1);
  Eigen::Vector3d T12 = util.getRelativePose_T21(R2, R1, T2, T1);  
  Eigen::Matrix3d F12 = util.getFundamentalMatrix(K2.inverse(), K1.inverse(), R12, T12);  
  // Initializations for paired edges between Hypo1 and Hypo 2
  Eigen::MatrixXd paired_edge = Eigen::MatrixXd::Constant(Edges_pairs.rows(),50,-2);
  // Compute epipolar wedges between Hypo1 and Hypo2 and find the angle range 1
  // std::cout<< "Here" <<std::endl;
  Eigen::MatrixXd OreListdegree    = getOre.getOreList(Edges_HYPO2, All_R, All_T, K1, K2);
  
  Eigen::MatrixXd OreListBardegree = getOre.getOreListBar(Edges_HYPO1, All_R, All_T, K1, K2, HYPO2_VIEW_INDX, HYPO1_VIEW_INDX);
  if(NOT1STROUND == 1){
    for(int h1_idx = 0; h1_idx < edge_idx_H1.size(); h1_idx++){
      if(edge_idx_H1[h1_idx]>Edges_HYPO1.rows()){
        std::cout<< "h1_idx: " << h1_idx << std::endl;
        break;
      }
      paired_edge(edge_idx_H1[h1_idx],0) = -3;
    }
  }
  // std::cout<< "Edges_HYPO1: \n" << Edges_HYPO1.block(0,0,20,2) << std::endl;
  // std::cout<< "OreListBardegree: \n" << OreListBardegree.block(0,0,20,2) << std::endl;
  // Eigen::MatrixXd OreListdegree    = getOre.getOreList(Edges_HYPO2, All_R, All_T, K1, K2);
  // Calculate the angle range for epipolar lines (Hypo1 --> Hypo2)
  // double angle_range1              = OreListdegree.maxCoeff() - OreListdegree.minCoeff();
  // Calculate the angle range for epipolar wedges (Hypo1 --> Hypo2)
  // double range1                    =  angle_range1;
  // std::cout << "angle_range1: " << angle_range1 <<std::endl;

  ///////////////////////////////////////////////////////////////////
  //> Compute all relative poses and fundamental matrices
  ///////////////////////////////////////////////////////////////////
  /*
  std::vector<Eigen::Matrix3d> Rel_Rot;
  std::vector<Eigen::Vector3d> Rel_Transl;
  std::vector<Eigen::Matrix3d> F31s;
  Eigen::Matrix3d K_vi;
  for (int vi = 0; vi < DATASET_NUM_OF_FRAMES; vi++) {
    if (vi == HYPO1_VIEW_INDX || vi == HYPO2_VIEW_INDX) continue;
    
    Eigen::Matrix3d R3  = All_R[ vi ];
    Eigen::Vector3d T3  = All_T[ vi ];
    Eigen::Matrix3d R31 = util.getRelativePose_R21(R1, R3);
    Eigen::Vector3d T31 = util.getRelativePose_T21(R1, R3, T1, T3);
    Rel_Rot.push_back( R31 );
    Rel_Transl.push_back( T31 );

    if(IF_MULTIPLE_K == 1){
      K1 = All_K[HYPO1_VIEW_INDX];
      K_vi = All_K[vi];
    } else{
      K1 = K;
      K_vi = K;
    }
    Eigen::Matrix3d F31 = util.getFundamentalMatrix(K_vi.inverse(), K1.inverse(), R31, T31);
    F31s.push_back(F31);
  }

  
  std::vector <int> idx_start;
  std::vector <int> idx_end;
  for (int VALID_INDX = 0; VALID_INDX < DATASET_NUM_OF_FRAMES; VALID_INDX++){
    if(VALID_INDX == HYPO1_VIEW_INDX){
      continue;
    }
    //////////////////////////////////////////////
    // find sted idx for all views --> HYPO1
    //////////////////////////////////////////////
    // Get camera pose and other info for current validation view
    
    Eigen::MatrixXd TO_Edges_VALID = All_Edgels[VALID_INDX];
    Eigen::Matrix3d R3             = All_R[VALID_INDX];
    Eigen::Vector3d T3             = All_T[VALID_INDX];
    Eigen::MatrixXd VALI_Orient    = TO_Edges_VALID.col(2);
    Eigen::MatrixXd Tangents_VALID;
    Tangents_VALID.conservativeResize(TO_Edges_VALID.rows(),2);
    Tangents_VALID.col(0)          = (VALI_Orient.array()).cos();
    Tangents_VALID.col(1)          = (VALI_Orient.array()).sin();
    // deal with multiple K scenario
    Eigen::Matrix3d K3;
    if(IF_MULTIPLE_K == 1){
      K3 = All_K[VALID_INDX];
    }else{
      K3 = K;
    }
    Eigen::MatrixXd OreListBardegree_pre = getOre.getOreListBarVali(Edges_HYPO1, All_R, All_T, K1, K3, VALID_INDX, HYPO1_VIEW_INDX);
    Eigen::MatrixXd OreListdegree_pre    = getOre.getOreListVali(TO_Edges_VALID, All_R, All_T, K1, K3, VALID_INDX, HYPO1_VIEW_INDX);
    double angle_range_pre               = OreListdegree_pre.maxCoeff() - OreListdegree_pre.minCoeff();
    double range_pre                     =  angle_range_pre * PERCENT_EPIPOLE;
    for(int edge_idx = 0; edge_idx < Edges_HYPO1.rows(); edge_idx++){
      double thresh_ore21_1_pre            = OreListBardegree_pre(edge_idx,0) - range_pre;
      double thresh_ore21_2_pre            = OreListBardegree_pre(edge_idx,0) + range_pre;
      Eigen::MatrixXd HYPO2_idxsted = PairHypo.getHYPO2_idx_Ore_sted(OreListdegree_pre, thresh_ore21_1_pre, thresh_ore21_2_pre);
      //std::cout<< "HYPO2_idxsted: \n" << HYPO2_idxsted <<std::endl;
      idx_start.push_back(int(HYPO2_idxsted(0,0)));
      idx_end.push_back(int(HYPO2_idxsted(1,0)));
    }
  }
  */

  /*
  for (int i = 0; i < idx_start.size(); i++) {
    std::cout << idx_start[i] << " ";
  }
  std::cout << std::endl;
  for (int i = 0; i < idx_end.size(); i++) {
    std::cout << idx_end[i] << " ";
  }
  std::cout << std::endl;
  std::cout << idx_end.size() << std::endl;
  */

  /*
  tend_pre = clock() - tstart_pre; 
  std::cout << "It took "<< double(tend_pre)/double(CLOCKS_PER_SEC) <<" second(s) to finish preprocessing."<<std::endl;
  */

  //<<<<<<<<< Core of the pipeline starts here >>>>>>>>>//
  std::cout<< "pipeline start" <<std::endl;
  
  //clock_t tstart, tstart1, tend;
  clock_t tstart, tend;
  double itime, ftime, exec_time, totaltime;
  totaltime = 0;
  int thresh_EDG = threshEDG;
  while(thresh_EDG >=threshEDGforall){
  std::cout<< "Number of edges in hypothesis view 1: " << Edges_HYPO1.rows() << std::endl;
  std::cout<< "Number of edges in hypothesis view 2: " << Edges_HYPO2.rows() << std::endl;
  Eigen::MatrixXd OreListdegree    = getOre.getOreList(Edges_HYPO2, All_R, All_T, K1, K2);
  Eigen::MatrixXd OreListBardegree = getOre.getOreListBar(Edges_HYPO1, All_R, All_T, K1, K2, HYPO2_VIEW_INDX, HYPO1_VIEW_INDX);
  //remove edge in H2
  std::cout<< "Edges_HYPO2.rows(): " << Edges_HYPO2.rows() << std::endl;
  if(NOT1STROUND == 1){
    for(int h2_idx = 0; h2_idx < edge_idx_H2.size(); h2_idx++){
      if(edge_idx_H2[h2_idx]>Edges_HYPO2.rows()){
        std::cout<< "edge_idx_H2[h2_idx]: " << edge_idx_H2[h2_idx] << std::endl;
        break;
      }
      Edges_HYPO2.row(edge_idx_H2[h2_idx]) << 0,0,0,0;
    }
    // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }
  }
  //<<<<<<<<< OpenMp Operation >>>>>>>>>//
  #if defined(_OPENMP)
    unsigned nthreads = NUM_OF_OPENMP_THREADS;
    omp_set_num_threads(nthreads);
    int ID = omp_get_thread_num();
    itime = omp_get_wtime();
    std::cout << "Using " << nthreads << " threads for OpenMP parallelization." << std::endl;
    std::cout << "nthreads: " << nthreads << "." << std::endl;
  #pragma omp parallel for schedule(static, nthreads) //reduction(+:variables_to_be_summed_up)   //> CH: comment out reduction if you have a variable to be summed up inside the first loop
  #endif

  //> First loop: loop over all edgels from hypothesis view 1
  
  
  for(int edge_idx = 0; edge_idx < Edges_HYPO1.rows() ; edge_idx++){
    if(Edges_HYPO1(edge_idx,0) < 10 || Edges_HYPO1(edge_idx,0) > imgcols-10 || Edges_HYPO1(edge_idx,1) < 10 || Edges_HYPO1(edge_idx,1) > imgrows-10){
      continue;
    }
    if(paired_edge(edge_idx,0) != -2){
      continue;
    }
    // Get the current edge from Hypo1
    Eigen::Vector3d pt_edgel_HYPO1;
    pt_edgel_HYPO1 << Edges_HYPO1(edge_idx,0), Edges_HYPO1(edge_idx,1), 1;
    // std::cout << "edge_idx: " << edge_idx <<std::endl;
    // Eigen::MatrixXd ApBp = PairHypo.getAp_Bp(Edges_HYPO2, pt_edgel_HYPO1, F);
    // Get the range of epipolar wedge for the current edge (Hypo1 --> Hypo2)
    double thresh_ore21_1 = OreListBardegree(edge_idx,0);
    double thresh_ore21_2 = OreListBardegree(edge_idx,1);
    // std::cout << "edge_idx: " << edge_idx <<std::endl;
    // std::cout << "thresh_ore21_1: " << thresh_ore21_1 <<std::endl;
    // std::cout << "thresh_ore21_2: " << thresh_ore21_2 <<std::endl;
    
    Eigen::MatrixXd HYPO2_idx_raw = PairHypo.getHYPO2_idx_Ore(OreListdegree, thresh_ore21_1, thresh_ore21_2);
    // std::cout << "HYPO2_idx_raw: \n" << HYPO2_idx_raw.rows() <<std::endl;
    // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }
    if (HYPO2_idx_raw.rows() == 0){
      continue;
    }
    // std::cout << "edge_idx: " << edge_idx <<std::endl;
    // std::cout << "HYPO2_idx_raw: \n" << HYPO2_idx_raw.rows() <<std::endl;
    
    // std::cout << "HYPO2_idxsted: \n" << HYPO2_idxsted <<std::endl;
    Eigen::MatrixXd edgels_HYPO2 = PairHypo.getedgels_HYPO2_Ore(Edges_HYPO2, OreListdegree, thresh_ore21_1, thresh_ore21_2);
    Eigen::MatrixXd edgel_HYPO1  = Edges_HYPO1.row(edge_idx);
    Eigen::MatrixXd edgels_HYPO2_corrected = PairHypo.edgelsHYPO2correct(edgels_HYPO2, edgel_HYPO1, F, F12, HYPO2_idx_raw);
    Eigen::MatrixXd Edges_HYPO1_final(edgels_HYPO2_corrected.rows(),4);
    Edges_HYPO1_final << edgels_HYPO2_corrected.col(0), edgels_HYPO2_corrected.col(1), edgels_HYPO2_corrected.col(2), edgels_HYPO2_corrected.col(3);
    Eigen::MatrixXd Edges_HYPO2_final(edgels_HYPO2_corrected.rows(),4);
    Edges_HYPO2_final << edgels_HYPO2_corrected.col(4), edgels_HYPO2_corrected.col(5), edgels_HYPO2_corrected.col(6), edgels_HYPO2_corrected.col(7);
    Eigen::MatrixXd HYPO2_idx(edgels_HYPO2_corrected.rows(),1); 
    HYPO2_idx << edgels_HYPO2_corrected.col(8);
    if (HYPO2_idx.rows() == 0){
      continue;
    }
    // std::cout << "Edges_HYPO1_final: \n" << Edges_HYPO1_final <<std::endl;
    // std::cout << "Edges_HYPO2_final: \n" << Edges_HYPO2_final <<std::endl;

    // Initializations for all validation views
    int VALID_idx = 0;
    int stack_idx = 0;
    Eigen::MatrixXd supported_indices;
    supported_indices.conservativeResize(edgels_HYPO2.rows(),DATASET_NUM_OF_FRAMES-2);
    Eigen::MatrixXd supported_indice_current;
    supported_indice_current.conservativeResize(edgels_HYPO2.rows(),1);
    Eigen::MatrixXd supported_indices_stack;
    
    bool isempty_link = true;
    //tstart = clock();

    //> second loop: loop over all validation views
    for (int VALID_INDX = 0; VALID_INDX < DATASET_NUM_OF_FRAMES; VALID_INDX++){
      if(VALID_INDX == HYPO1_VIEW_INDX || VALID_INDX == HYPO2_VIEW_INDX){
        continue;
      }
      // Get camera pose and other info for current validation view
      Eigen::MatrixXd TO_Edges_VALID = All_Edgels[VALID_INDX];
      Eigen::Matrix3d R3             = All_R[VALID_INDX];
      Eigen::Vector3d T3             = All_T[VALID_INDX];
      Eigen::MatrixXd VALI_Orient    = TO_Edges_VALID.col(2);
      Eigen::MatrixXd Tangents_VALID;
      Tangents_VALID.conservativeResize(TO_Edges_VALID.rows(),2);
      Tangents_VALID.col(0)          = (VALI_Orient.array()).cos();
      Tangents_VALID.col(1)          = (VALI_Orient.array()).sin();
      // deal with multiple K scenario
      Eigen::Matrix3d K3;
      if(IF_MULTIPLE_K == 1){
        K3 = All_K[VALID_INDX];
      }else{
        K3 = K;
      }
      // Calculate relative pose
      Eigen::Matrix3d R31 = util.getRelativePose_R21(R1, R3);
      Eigen::Vector3d T31 = util.getRelativePose_T21(R1, R3, T1, T3);
      
      // Calculate the angle range for epipolar lines (Hypo1 --> Vali)
      Eigen::MatrixXd pt_edge            = Edges_HYPO1_final;
      Eigen::MatrixXd edge_tgt_gamma3    = getReprojEdgel.getGamma3Tgt(pt_edge, Edges_HYPO2_final, All_R, All_T, VALID_INDX, K1, K2);
      Eigen::MatrixXd OreListBardegree31 = getOre.getOreListBar(pt_edge, All_R, All_T, K1, K3, VALID_INDX, HYPO1_VIEW_INDX);
      Eigen::MatrixXd OreListdegree31    = getOre.getOreListVali(TO_Edges_VALID, All_R, All_T, K1, K3, VALID_INDX, HYPO1_VIEW_INDX);
      // double angle_range2   = OreListdegree31.maxCoeff() - OreListdegree31.minCoeff();
      // Calculate the angle range for epipolar wedges (Hypo1 --> Vali)
      // double range2         =  angle_range2; // * PERCENT_EPIPOLE;
      // Get the range of epipolar wedge for the current edge (Hypo1 --> Vali)
      // std::cout << "edge_tgt_gamma3: \n" << edge_tgt_gamma3 <<std::endl;

      // Find all the edges fall inside epipolar wedge on validation view (Hypo1 --> Vali)
      
      Eigen::MatrixXd OreListBardegree32 = getOre.getOreListBar(Edges_HYPO2_final, All_R, All_T, K2, K3, VALID_INDX, HYPO2_VIEW_INDX);
      Eigen::MatrixXd OreListdegree32    = getOre.getOreListVali(TO_Edges_VALID, All_R, All_T, K2, K3, VALID_INDX, HYPO2_VIEW_INDX);
      Eigen::VectorXd isparallel         = Eigen::VectorXd::Ones(Edges_HYPO2_final.rows());
      for (int idx_pair = 0; idx_pair < Edges_HYPO2_final.rows(); idx_pair++){
        // Calculate the angle range for epipolar lines (Hypo2 --> Vali)
        // double angle_range3   = OreListdegree32.maxCoeff() - OreListdegree32.minCoeff();
        // Calculate the angle range for epipolar wedges (Hypo2 --> Vali)
        // double range3         =  angle_range3;// * PERCENT_EPIPOLE;
        // Get the range of epipolar wedge for the current edge (Hypo2 --> Vali)
        double thresh_ore31_1 = OreListBardegree31(idx_pair,0);
        double thresh_ore31_2 = OreListBardegree31(idx_pair,1);
        // std::cout << "thresh_ore31_1: " << thresh_ore31_1 <<std::endl;
        // std::cout << "thresh_ore31_2: " << thresh_ore31_2 <<std::endl;
        double thresh_ore32_1 = OreListBardegree32(idx_pair,0);
        double thresh_ore32_2 = OreListBardegree32(idx_pair,1);
        // std::cout << "thresh_ore32_1: " << thresh_ore32_1 <<std::endl;
        // std::cout << "thresh_ore32_2: " << thresh_ore32_2 <<std::endl;
        
        Eigen::MatrixXd vali_idx31 = PairHypo.getHYPO2_idx_Ore(OreListdegree31, thresh_ore31_1, thresh_ore31_2);
        Eigen::MatrixXd edgels_31  = PairHypo.getedgels_HYPO2_Ore(TO_Edges_VALID, OreListdegree31, thresh_ore31_1, thresh_ore31_2);
        // std::cout<<"vali_idx31: \n"<<vali_idx31.rows()<<std::endl;
        // std::cout<<"edgels_31: \n"<<edgels_31<<std::endl;

        // Find all the edges fall inside epipolar wedge on validation view (Hypo2 --> Vali)
        Eigen::MatrixXd vali_idx32 = PairHypo.getHYPO2_idx_Ore(OreListdegree32, thresh_ore32_1, thresh_ore32_2);
        Eigen::MatrixXd edgels_32  = PairHypo.getedgels_HYPO2_Ore(TO_Edges_VALID, OreListdegree32, thresh_ore32_1, thresh_ore32_2);
        // std::cout<<"vali_idx32: \n"<<vali_idx32.rows()<<std::endl;
        // std::cout<<"edgels_32: \n"<<edgels_32<<std::endl;
        
        // Check if the two wedges could be considered as parallel to each other
        
        Eigen::MatrixXd anglediff(4,1);
        anglediff << fabs(thresh_ore31_1 - thresh_ore32_1), 
                     fabs(thresh_ore31_1 - thresh_ore32_2),
                     fabs(thresh_ore31_2 - thresh_ore32_1),
                     fabs(thresh_ore31_2 - thresh_ore32_2);
        // std::cout<<"anglediff: \n"<<anglediff<<std::endl;
        // std::cout<<"vali_idx32: \n"<<vali_idx32<<std::endl;
        if(anglediff.maxCoeff() <= parallelangle){
          isparallel.row(idx_pair) << 0;
        }

        // Find all the edges fall inside the two epipolar wedges intersection on validation view 
        // (Hypo1 --> Vali) && (Hypo2 --> Vali)
        std::vector<double> v_intersection;
        std::vector<double> v1(vali_idx31.data(), vali_idx31.data() + vali_idx31.rows());
        std::vector<double> v2(vali_idx32.data(), vali_idx32.data() + vali_idx32.rows());
        set_intersection(v1.begin(), v1.end(), v2.begin(), v2.end(), back_inserter(v_intersection));
        Eigen::VectorXd idxVector = Eigen::Map<Eigen::VectorXd, Eigen::Unaligned>(v_intersection.data(), v_intersection.size());
        Eigen::MatrixXd inliner(idxVector);
        
        // Caluclate orientation of gamma 3
        Eigen::Vector2d edgels_tgt_reproj = {edge_tgt_gamma3(idx_pair,0), edge_tgt_gamma3(idx_pair,1)};
        // std::cout<<"edgels_tgt_reproj: \n"<<edgels_tgt_reproj<<std::endl;
        // Get support from validation view for this gamma 3
        double supported_link_indx = getSupport.getSupportIdx(edgels_tgt_reproj, Tangents_VALID, inliner);
        // std::cout << "supported_link_indx: " << supported_link_indx <<std::endl;
        // std::cout << "inliner: " << inliner <<std::endl;
        // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }

        // Get the supporting edge idx from this validation view (if isnotparallel)
        if (isparallel(idx_pair,0) != 0){
          supported_indice_current.row(idx_pair) << supported_link_indx;
        }else{
          supported_indice_current.row(idx_pair) << -2;
        }
        if (supported_link_indx != -2 && isparallel(idx_pair,0) != 0){
          supported_indices_stack.conservativeResize(stack_idx+1,2);
          supported_indices_stack.row(stack_idx) << double(idx_pair), double(supported_link_indx);
          isempty_link = false;
          stack_idx++;
        }
      }
      supported_indices.col(VALID_idx) << supported_indice_current.col(0);
      // std::cout << "isparallel: \n" << isparallel << std::endl;
      // std::cout << "supported_indices.col(VALID_idx): \n" << supported_indices.col(VALID_idx) << std::endl;
      VALID_idx++;
    } //> End of second loop
    
    // std::cout << "supported_indices_stack: " << supported_indices_stack.rows() <<std::endl;
    // std::cout << "supported_indices_stack: \n" << supported_indices_stack <<std::endl;
    // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }

    //tend = clock() - tstart; 
    //std::cout << "It took "<< double(tend)/double(CLOCKS_PER_SEC) <<" second(s) to get support from validation views."<<std::endl;
    if(isempty_link){
      continue;
    }

    std::vector<double> indices_stack(supported_indices_stack.data(), supported_indices_stack.data() + supported_indices_stack.rows());
    std::vector<double> indices_stack_unique = indices_stack;
    std::sort(indices_stack_unique.begin(), indices_stack_unique.end());
    std::vector<double>::iterator it1;
    it1 = std::unique(indices_stack_unique.begin(), indices_stack_unique.end());
    indices_stack_unique.resize( std::distance(indices_stack_unique.begin(),it1) );

    Eigen::VectorXd rep_count;
    rep_count.conservativeResize(indices_stack_unique.size(),1);

    for(int unique_idx = 0; unique_idx<indices_stack_unique.size(); unique_idx++){
      rep_count.row(unique_idx) << double(count(indices_stack.begin(), indices_stack.end(), indices_stack_unique[unique_idx]));
    }

    Eigen::VectorXd::Index   maxIndex;
    double max_support = rep_count.maxCoeff(&maxIndex);
    int numofmax = std::count(rep_count.data(), rep_count.data()+rep_count.size(), max_support);
    // std::cout << "num of max: "<< numofmax<<std::endl;
    
    if( double(max_support) < MAX_NUM_OF_SUPPORT_VIEWS){
      continue;
    }
    int finalpair = -2;
    if(numofmax > 1){
      std::vector<double> rep_count_vec(rep_count.data(), rep_count.data() + rep_count.rows());
      //std::cout<< "here"<<std::endl;
      std::vector<int> max_index;
      auto start_it = begin(rep_count_vec);
      while (start_it != end(rep_count_vec)) {
        start_it = std::find(start_it, end(rep_count_vec), max_support);
        if (start_it != end(rep_count_vec)) {
          auto const pos = std::distance(begin(rep_count_vec), start_it);
          max_index.push_back(int(pos));
          ++start_it;
        }
      }
      Eigen::Vector3d coeffs;
      coeffs = F * pt_edgel_HYPO1;
      Eigen::MatrixXd Edge_Pts;
      Edge_Pts.conservativeResize(max_index.size(),2);
      for(int maxidx = 0; maxidx<max_index.size(); maxidx++){
        Edge_Pts.row(maxidx) << Edges_HYPO2_final(indices_stack_unique[max_index[maxidx]], 0),Edges_HYPO2_final(indices_stack_unique[max_index[maxidx]], 1) ;
      }
      Eigen::VectorXd Ap = coeffs(0)*Edge_Pts.col(0);
      Eigen::VectorXd Bp = coeffs(1)*Edge_Pts.col(1);
      Eigen::VectorXd numDist = Ap + Bp + Eigen::VectorXd::Ones(Ap.rows())*coeffs(2);
      double denomDist = coeffs(0)*coeffs(0) + coeffs(1)*coeffs(1);
      denomDist = sqrt(denomDist);
      Eigen::VectorXd dist = numDist.cwiseAbs()/denomDist;
      //std::cout << dist <<std::endl;
      Eigen::VectorXd::Index   minIndex;
      double min_dist = dist.minCoeff(&minIndex);
      // std::cout << "min_dist: "<< min_dist <<std::endl;
      if(min_dist > DIST_THRESH){
        continue;
      }
      finalpair = int(indices_stack_unique[max_index[minIndex]]);
      // std::cout << "Multi: " << finalpair <<std::endl;
      // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }
    }else{
      finalpair = int(indices_stack_unique[int(maxIndex)]);
      // std::cout << "single: " << finalpair <<std::endl;
    }
    // linearTriangulation code already exist
    paired_edge.row(edge_idx) << edge_idx, HYPO2_idx(finalpair), supported_indices.row(finalpair);
    //std::cout << "paired_edge.row(edge_idx): \n" << paired_edge.row(edge_idx) <<std::endl;
    /*
    Eigen::Vector2d pt_H1 = Edges_HYPO1_final.row(finalpair);
    Eigen::Vector2d pt_H2 = Edges_HYPO2_final.row(finalpair);
    std::vector<Eigen::Vector2d> pts;
    pts.push_back(pt_H1);
    pts.push_back(pt_H2);
    // std::cout << "pt1: " << pt_H1 <<std::endl;
    // std::cout << "pt2: " << pt_H2 <<std::endl;
    std::vector<Eigen::Matrix3d> Rs;
    Rs.push_back(R21);
    std::vector<Eigen::Vector3d> Ts;
    Ts.push_back(T21);
    std::vector<double> K1_v;
    K1_v.push_back(K1(0,2));
    K1_v.push_back(K1(1,2));
    K1_v.push_back(K1(0,0));
    K1_v.push_back(K1(1,1));
    // std::cout << "K1_v: " << K1_v[0] <<"; "<< K1_v[1] <<"; " << K1_v[2] <<"; " << K1_v[3] <<std::endl;
    // std::cout << "K1: " << K1 <<std::endl;
    Eigen::Vector3d edge_pt_3D = linearTriangulation(2, pts, Rs,Ts,K1_v);
    //std::cout << "edge_pt_3D: " << edge_pt_3D <<std::endl;
    */
    // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }
  }
  //> End of first loop4

  #if defined(_OPENMP)
    ftime = omp_get_wtime();
    exec_time = ftime - itime;
    totaltime += exec_time;
    std::cout << "It took "<< exec_time <<" second(s) to finish this round."<<std::endl;
    std::cout << "End of using OpenMP parallelization." << std::endl;
  #endif
  // std::cout<< "Edges_HYPO1: " << Edges_HYPO1.rows() << std::endl;


/*
 #if defined(_OPENMP)
    ftime = omp_get_wtime();
    exec_time = ftime - itime;
    std::cout << "It took "<< exec_time <<" second(s) to finish the whole pipeline."<<std::endl;
    std::cout << "End of using OpenMP parallelization." << std::endl;
  #endif
*/

  //> CH: Make pair_edge locally, and merge them to a global variable once the for loop is finished.
  // .....
  tstart = clock();
  int pair_num = 0;
  Eigen::MatrixXd paired_edge_final;
  for(int pair_idx = 0; pair_idx < paired_edge.rows(); pair_idx++){
    if(paired_edge(pair_idx,0) != -2 && paired_edge(pair_idx,0) != -3){
      paired_edge_final.conservativeResize(pair_num+1,50);
      paired_edge_final.row(pair_num) << paired_edge.row(pair_idx);
      pair_num++;
    }
  }
  Eigen::MatrixXd Gamma1s;
   // gamma3 calculation, next view index pre
  if(thresh_EDG == 1){
  std::vector<Eigen::Matrix3d> Rs;
  Rs.push_back(R21);
  std::vector<Eigen::Vector3d> Ts;
  Ts.push_back(T21);
  std::vector<double> K1_v;
  K1_v.push_back(K1(0,2));
  K1_v.push_back(K1(1,2));
  K1_v.push_back(K1(0,0));
  K1_v.push_back(K1(1,1));
  Gamma1s.conservativeResize(paired_edge_final.rows(),3);
  for(int pair_idx = 0; pair_idx < paired_edge_final.rows(); pair_idx++){
    Eigen::MatrixXd edgel_HYPO1   = Edges_HYPO1_all.row(int(paired_edge_final(pair_idx,0)));
    Eigen::MatrixXd edgels_HYPO2  = Edges_HYPO2_all.row(int(paired_edge_final(pair_idx,1)));
    // std::cout<< "edgels_HYPO2: " << edgels_HYPO2 << std::endl;
    // std::cout<< "paired_edge_final: " << paired_edge_final(0,0) << std::endl;
    // std::cout<< "paired_edge_final: " << paired_edge_final(0,1) << std::endl;
    Eigen::MatrixXd HYPO2_idx_raw = Edges_HYPO1.row(int(paired_edge_final(pair_idx,0)));
    // std::cout<< "here \n" << std::endl;
    Eigen::MatrixXd edgels_HYPO2_corrected = PairHypo.edgelsHYPO2correct(edgels_HYPO2, edgel_HYPO1, F, F12, HYPO2_idx_raw);
    // std::cout<< "edgels_HYPO2_corrected: \n" << edgels_HYPO2_corrected <<std::endl;
    Eigen::MatrixXd Edges_HYPO1_final(edgels_HYPO2_corrected.rows(),4);
    Edges_HYPO1_final << edgels_HYPO2_corrected.col(0), edgels_HYPO2_corrected.col(1), edgels_HYPO2_corrected.col(2), edgels_HYPO2_corrected.col(3);
    Eigen::MatrixXd Edges_HYPO2_final(edgels_HYPO2_corrected.rows(),4);
    Edges_HYPO2_final << edgels_HYPO2_corrected.col(4), edgels_HYPO2_corrected.col(5), edgels_HYPO2_corrected.col(6), edgels_HYPO2_corrected.col(7);
    
    Eigen::Vector2d pt_H1 = Edges_HYPO1_final.row(0);
    Eigen::Vector2d pt_H2 = Edges_HYPO2_final.row(0);
    std::vector<Eigen::Vector2d> pts;
    pts.push_back(pt_H1);
    pts.push_back(pt_H2);

    Eigen::Vector3d edge_pt_3D = linearTriangulation(2, pts, Rs,Ts,K1_v);
    // std::cout<< "edge_pt_3D: \n" << edge_pt_3D << std::endl;
    Gamma1s.row(pair_idx)<< edge_pt_3D(0),edge_pt_3D(1),edge_pt_3D(2);
  }
  }
  // std::cout<< "Gamma1s: \n" << Gamma1s.row(0) << std::endl;
  std::cout<< "this round finished" <<std::endl;

  //std::cout<< "pipeline finished" <<std::endl;
  tend = clock() - tstart; 
  std::cout << "It took "<< double(tend)/double(CLOCKS_PER_SEC) <<" second(s) to generate the final edge pair for output file."<<std::endl;
  std::cout << "Number of pairs found till this round: " << paired_edge_final.rows()<<std::endl;



  /*
  std::ofstream myfile1;
  std::string Output_File_Path = OUTPUT_WRITE_FOLDER + "pairededge_ABC0006_"+std::to_string(HYPO1_VIEW_INDX)+"n"+std::to_string(HYPO2_VIEW_INDX)+"_t32to"+std::to_string(thresh_EDG)+"excludehypo1n2_delta1.txt";
  myfile1.open (Output_File_Path);
  myfile1 << paired_edge_final;
  myfile1.close();
  */

  thresh_EDG = thresh_EDG/2;
  std::vector<Eigen::MatrixXd> All_Edgels_H12_1;  
  if(thresh_EDG >=1){
  while(file_idx < 3) {
    if(file_idx == 1){
      H_idx = HYPO1_VIEW_INDX;
    }else{
      H_idx = HYPO2_VIEW_INDX;
    }
    
    // std::cout<< "thresh_EDG: " << thresh_EDG << std::endl;
    // std::string Edge_File_PathH12 = REPO_DIR + "datasets/ABC-NEF/00000006/Edges/thresh/Edge_"+std::to_string(H_idx+1)+"_t"+std::to_string(thresh_EDG)+".txt"; 
    std::string Edge_File_PathH12 = REPO_DIR + "datasets/ABC-NEF/00000006/Edges/Edge_"+std::to_string(H_idx+1)+"_t"+std::to_string(thresh_EDG)+".txt"; 
    file_idx ++;
    Eigen::MatrixXd Edgels; //> Declare loclly, ensuring the memory addresses are different for different frames
    Edge_File.open(Edge_File_PathH12, std::ios_base::in);
    if (!Edge_File) { 
       std::cerr << "Edge file not existed!\n"; exit(1); 
    }
    else {
      Edgels.resize(1,4);
      while (Edge_File >> rd_data) {
        row_edge(q) = rd_data;
        q++;
        if(q>3){
          Edgels.conservativeResize(d+1,4);
          Edgels.row(d) = row_edge;
          q = 0;
          d++;
        }
      }
      Edge_File.close();
      All_Edgels_H12_1.push_back(Edgels);
      //Edgels = {};
      d = 0;
      q = 0;
    }
  }
  file_idx = 1;
  // std::cout<< "HYPO1 and HYPO2 Edge file loading finished" <<std::endl;

  Edges_HYPO1 = All_Edgels_H12_1[0];
  Edges_HYPO2 = All_Edgels_H12_1[1];

  //*
  
  //*/
  // std::cout<< "Edges_HYPO2: \n" << Edges_HYPO2.block(0,0,100,2) << std::endl;
 

  for(int pair_idx = 0; pair_idx < paired_edge.rows(); pair_idx++){
    if(paired_edge(pair_idx,0) != -2 && paired_edge(pair_idx,0) != -3 ){
      Edges_HYPO2.row(int(paired_edge(pair_idx,1))) << 0,0,0,0;
      pair_num++;
    }
  }
  
  // if (DEBUG == 1) { std::cerr << "\n—=>DEBUG MODE<=—\n"; exit(1); }
  }else{
    std::ofstream myfile1;
    std::string Output_File_Path = OUTPUT_WRITE_FOLDER + "pairededge_ABC0006_"+std::to_string(HYPO1_VIEW_INDX)+"n"+std::to_string(HYPO2_VIEW_INDX)+"_t32to"+std::to_string(thresh_EDG)+"excludehypo1n2_delta"+ deltastr +"_theta"+std::to_string(OREN_THRESH)+"_N"+std::to_string(MAX_NUM_OF_SUPPORT_VIEWS)+".txt";
    myfile1.open (Output_File_Path);
    myfile1 << paired_edge_final;
    myfile1.close();
    std::cout<< "pipeline finished" <<std::endl;
    std::cout << "It took "<< totaltime <<" second(s) to finish the whole pipeline."<<std::endl;
    std::ofstream myfile2;
    std::string Output_File_Path2 = OUTPUT_WRITE_FOLDER + "Gamma1s_ABC0006_"+std::to_string(HYPO1_VIEW_INDX)+"n"+std::to_string(HYPO2_VIEW_INDX)+"_t32to"+std::to_string(thresh_EDG)+"excludehypo1n2_delta"+ deltastr +"_theta"+std::to_string(OREN_THRESH)+"_N"+std::to_string(MAX_NUM_OF_SUPPORT_VIEWS)+".txt";
    myfile2.open (Output_File_Path2);
    myfile2 << Gamma1s;
    myfile2.close();
  }

  }


}
