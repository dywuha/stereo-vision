#ifndef OPTIMIZATION_DEFORMATION_FIELD_SOLVER_
#define OPTIMIZATION_DEFORMATION_FIELD_SOLVER_

#include <ceres/ceres.h>
#include <ceres/rotation.h>

#include "../../tracker/stereo/stereo_tracker_base.h"


namespace optim
{

class DeformationFieldSolver
{
 public:
  DeformationFieldSolver(const double* const cam_intr, int bin_rows, int bin_cols,
                         int img_rows, int img_cols, std::string save_filepath);
  ~DeformationFieldSolver();
  void UpdateTracks(const track::StereoTrackerBase& tracker, const cv::Mat& Rt);
  void UpdateReverseTracks(const track::StereoTrackerBase& tracker, const cv::Mat& Rt);
  void Solve();

  // Factory to hide the construction of the CostFunction object from the client code.
  void AddCostResidual(const core::Point& left_prev, const core::Point& left_curr,
                       const core::Point& right_prev, const core::Point& right_curr,
                       const double* const cam_intr,
                       const std::array<double,3>& cam_trans,
                       const std::array<double,3>& cam_rot,
                       ceres::Problem& ceres_problem);

 private:
  int GetBinNum(const core::Point& pt);
  void ConvertArrayToMatrix(const double* left_dx_, cv::Mat& mat);

  std::vector<std::vector<std::tuple<core::Point,core::Point>>> left_tracks_, right_tracks_;
  std::vector<std::vector<int>> age_;
  std::vector<cv::Mat> gt_rt_;
  std::vector<std::array<double,3>> cam_translation_, cam_rotation_;

  int bin_rows_, bin_cols_;
  int img_rows_, img_cols_;
  int num_bins_;
  double bin_width_, bin_height_;
  double *left_dx_, *right_dx_;
  double *left_dy_, *right_dy_;
  uint64_t* left_num_points_;
  uint64_t* right_num_points_;
  double cam_intr_[5];
  std::string save_filepath_;
};

inline
void DeformationFieldSolver::ConvertArrayToMatrix(const double* def, cv::Mat& mat)
{
  mat = cv::Mat::zeros(bin_rows_, bin_cols_, CV_64F);
  for (int i = 0; i < bin_rows_; i++) {
    for (int j = 0; j < bin_cols_; j++) {
      mat.at<double>(i,j) = def[i*bin_cols_ + j];
    }
  }
}

inline
int DeformationFieldSolver::GetBinNum(const core::Point& pt)
{
  int c = pt.x_ / bin_width_;
  int r = pt.y_ / bin_height_;
  int bin_num = r * bin_cols_ + c;
  return bin_num;
}


namespace
{

template <typename T>
void ComputeReprojectionErrorResidual(
  const T* const lp_dx,
  const T* const lp_dy,
  const T* const lc_dx,
  const T* const lc_dy,
  const T* const rp_dx,
  //const T* const rp_dy,
  const T* const rc_dx,
  const T* const rc_dy,
  const double* const cam_intr,
  const double* const cam_trans,
  const double* const cam_rot,
  const core::Point& left_prev,
  const core::Point& right_prev,
  const core::Point& left_curr,
  const core::Point& right_curr,
  T* out_residuals)
{
  // left previous point
  T lp_x = left_prev.x_ + lp_dx[0];
  T lp_y = left_prev.y_ + lp_dy[0];

  // left current point
  T lc_x = left_curr.x_ + lc_dx[0];
  T lc_y = left_curr.y_ + lc_dy[0];

  // right previous point
  T rp_x = right_prev.x_ + rp_dx[0];

  // right current point
  T rc_x = right_curr.x_ + rc_dx[0];
  T rc_y = right_curr.y_ + rc_dy[0];

  T f = T(cam_intr[0]);
  //T fy = T(cam_intr[1]);
  T cx = T(cam_intr[2]);
  T cy = T(cam_intr[3]);
  T b = T(cam_intr[4]);

  // triangulate the 3d point
  T disp = lp_x - rp_x;
  //if (disp < 0.0)
  //  std::cout << "Negative disp! = "<< disp << "\n";
  // TODO
  //if (disp < 0.01) throw 1;
  //if (disp < 0.001) disp = T(0.001);
  T pt3d_prev[3];
  pt3d_prev[0] = (lp_x - cx) * b / disp;
  pt3d_prev[1] = (lp_y - cy) * b / disp;
  pt3d_prev[2] = f * b / disp;

  T pt3d[3];
  T cam_rotation[3];
  for (int i = 0; i < 3; i++)
    cam_rotation[i] = T(cam_rot[i]);
  // Apply the angle-axis camera rotation
  ceres::AngleAxisRotatePoint(cam_rotation, pt3d_prev, pt3d);
  // Apply the camera translation
  for (int i = 0; i < 3; i++)
    pt3d[i] = pt3d[i] + cam_trans[i];

  // Transform the point from homogeneous to euclidean
  T xe = pt3d[0] / pt3d[2];  // x / z
  T ye = pt3d[1] / pt3d[2];  // y / z

  // Apply the focal length
  T predict_lc_x = f * xe + cx;
  T predict_lc_y = f * ye + cy;
  //std::cout << predicted_x_left << " -- " << predicted_y_left << "\n";

  // now for right camera
  // first move point in right cam coord system
  pt3d[0] -= b;
  xe = pt3d[0] / pt3d[2];  // x / z
  ye = pt3d[1] / pt3d[2];  // y / z
  T predict_rc_x = f * xe + cx;
  T predict_rc_y = f * ye + cy;

  // Compute and return the error is the difference between the predicted and observed position
  //out_residuals[0] = predict_lc_x - T(lc_x);
  //out_residuals[1] = predict_lc_y - T(lc_y);
  //out_residuals[2] = predict_rc_x - T(rc_x);
  //out_residuals[3] = predict_rc_y - T(rc_y);
  T regularization_term = T(1.0) * ((lp_dx*lp_dx) + (lp_dy*lp_dy) + (lc_dx*lc_dx) + (lc_dy*lc_dy)
                                    + (rp_dx*rp_dx) + (rc_dx*rc_dx) + (rc_dy*rc_dy));
  out_residuals[0] = predict_lc_x - T(lc_x) + regularization_term;
  out_residuals[1] = predict_lc_y - T(lc_y) + regularization_term;
  out_residuals[2] = predict_rc_x - T(rc_x) + regularization_term;
  out_residuals[3] = predict_rc_y - T(rc_y) + regularization_term;
}

struct ReprojectionErrorResidual
{
  ReprojectionErrorResidual(const core::Point& left_prev, const core::Point& left_curr,
                            const core::Point& right_prev, const core::Point& right_curr,
                            const double* const cam_intr,
                            const std::array<double,3>& cam_trans,
                            const std::array<double,3>& cam_rot)
  {
    pt_left_prev_ = left_prev;
    pt_left_curr_ = left_curr;
    pt_right_prev_ = right_prev;
    pt_right_curr_ = right_curr;

    for (int i = 0; i < 5; i++) {
      cam_intr_[i] = cam_intr[i];    // 5 params - fx fy cx cy b
      if (i < 3) {
        cam_trans_[i] = cam_trans[i];
        cam_rot_[i] = cam_rot[i];
      }
    }
  }

  template <typename T>
  bool operator()(
      const T* const lp_dx, const T* const lp_dy,
      const T* const lc_dx, const T* const lc_dy,
      //const T* const rp_dx, const T* const rp_dy,
      const T* const rp_dx,
      const T* const rc_dx, const T* const rc_dy,
      T* out_residuals) const
  {
    ComputeReprojectionErrorResidual(
        lp_dx, lp_dy,
        lc_dx, lc_dy,
        rp_dx,
        //rp_dx, rp_dy,
        rc_dx, rc_dy,
        cam_intr_,       // cam intrinsics
        cam_trans_,
        cam_rot_,        // rotation in Euler angles
        pt_left_prev_,
        pt_right_prev_,
        pt_left_curr_,
        pt_right_curr_,
        out_residuals);
    return true;
  }

  core::Point pt_left_prev_;
  core::Point pt_right_prev_;
  core::Point pt_left_curr_;
  core::Point pt_right_curr_;
  double cam_intr_[5];
  double cam_trans_[3];
  double cam_rot_[3];
};


struct ReprojectionErrorResidualShareLeft
{
  ReprojectionErrorResidualShareLeft(const core::Point& left_prev, const core::Point& left_curr,
                                     const core::Point& right_prev, const core::Point& right_curr,
                                     const double* const cam_intr,
                                     const std::array<double,3>& cam_trans,
                                     const std::array<double,3>& cam_rot)
  {
    pt_left_prev_ = left_prev;
    pt_left_curr_ = left_curr;
    pt_right_prev_ = right_prev;
    pt_right_curr_ = right_curr;

    for (int i = 0; i < 5; i++) {
      cam_intr_[i] = cam_intr[i];    // 5 params - fx fy cx cy b
      if (i < 3) {
        cam_trans_[i] = cam_trans[i];
        cam_rot_[i] = cam_rot[i];
      }
    }
  }

  template <typename T>
  bool operator()(
      const T* const left_dx, const T* const left_dy,
      const T* const rp_dx,
      const T* const rc_dx, const T* const rc_dy,
      T* out_residuals) const
  {
    ComputeReprojectionErrorResidual(
        left_dx, left_dy,
        left_dx, left_dy,
        rp_dx,
        rc_dx, rc_dy,
        cam_intr_,       // cam intrinsics
        cam_trans_,
        cam_rot_,        // rotation in Euler angles
        pt_left_prev_,
        pt_right_prev_,
        pt_left_curr_,
        pt_right_curr_,
        out_residuals);
    return true;
  }

  core::Point pt_left_prev_;
  core::Point pt_right_prev_;
  core::Point pt_left_curr_;
  core::Point pt_right_curr_;
  double cam_intr_[5];
  double cam_trans_[3];
  double cam_rot_[3];
};

struct ReprojectionErrorResidualShareRight
{
  ReprojectionErrorResidualShareRight(const core::Point& left_prev, const core::Point& left_curr,
                                      const core::Point& right_prev, const core::Point& right_curr,
                                      const double* const cam_intr,
                                      const std::array<double,3>& cam_trans,
                                      const std::array<double,3>& cam_rot)
  {
    pt_left_prev_ = left_prev;
    pt_left_curr_ = left_curr;
    pt_right_prev_ = right_prev;
    pt_right_curr_ = right_curr;

    for (int i = 0; i < 5; i++) {
      cam_intr_[i] = cam_intr[i];    // 5 params - fx fy cx cy b
      if (i < 3) {
        cam_trans_[i] = cam_trans[i];
        cam_rot_[i] = cam_rot[i];
      }
    }
  }

  template <typename T>
  bool operator()(const T* const lp_dx, const T* const lp_dy,
                  const T* const lc_dx, const T* const lc_dy,
                  const T* const right_dx, const T* const right_dy,
                  T* out_residuals) const
  {
    ComputeReprojectionErrorResidual(
        lp_dx, lp_dy,
        lc_dx, lc_dy,
        right_dx,
        right_dx, right_dy,
        cam_intr_,       // cam intrinsics
        cam_trans_,
        cam_rot_,        // rotation in Euler angles
        pt_left_prev_,
        pt_right_prev_,
        pt_left_curr_,
        pt_right_curr_,
        out_residuals);
    return true;
  }

  core::Point pt_left_prev_;
  core::Point pt_right_prev_;
  core::Point pt_left_curr_;
  core::Point pt_right_curr_;
  double cam_intr_[5];
  double cam_trans_[3];
  double cam_rot_[3];
};


struct ReprojectionErrorResidualShareLeftAndRight
{
  ReprojectionErrorResidualShareLeftAndRight(const core::Point& left_prev, const core::Point& left_curr,
                                             const core::Point& right_prev, const core::Point& right_curr,
                                             const double* const cam_intr,
                                             const std::array<double,3>& cam_trans,
                                             const std::array<double,3>& cam_rot)
  {
    pt_left_prev_ = left_prev;
    pt_left_curr_ = left_curr;
    pt_right_prev_ = right_prev;
    pt_right_curr_ = right_curr;

    for (int i = 0; i < 5; i++) {
      cam_intr_[i] = cam_intr[i];    // 5 params - fx fy cx cy b
      if (i < 3) {
        cam_trans_[i] = cam_trans[i];
        cam_rot_[i] = cam_rot[i];
      }
    }
  }

  template <typename T>
  bool operator()(
      const T* const left_dx, const T* const left_dy,
      const T* const right_dx, const T* const right_dy,
      T* out_residuals) const
  {
    ComputeReprojectionErrorResidual(
        left_dx, left_dy,
        left_dx, left_dy,
        right_dx,
        right_dx, right_dy,
        cam_intr_,       // cam intrinsics
        cam_trans_,
        cam_rot_,        // rotation in Euler angles
        pt_left_prev_,
        pt_right_prev_,
        pt_left_curr_,
        pt_right_curr_,
        out_residuals);
    return true;
  }

  core::Point pt_left_prev_;
  core::Point pt_right_prev_;
  core::Point pt_left_curr_;
  core::Point pt_right_curr_;
  double cam_intr_[5];
  double cam_trans_[3];
  double cam_rot_[3];
};

} // end empty namespace

}

#endif
