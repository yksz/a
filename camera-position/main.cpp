#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

static std::string s_shownWindowName;
static cv::Mat s_shownImage;
static std::size_t s_maxClickedCount = 0;
static std::vector<cv::Point2f>* s_clickedPoints = NULL;

static void drawCross(cv::Mat& image, const cv::Point2f& point, const cv::Scalar& color,
        int length, int thickness = 1) {
    cv::line(image, cv::Point2f(point.x - length, point.y), cv::Point2f(point.x + length, point.y), color, thickness);
    cv::line(image, cv::Point2f(point.x, point.y - length), cv::Point2f(point.x, point.y + length), color, thickness);
}

/**
 * ファイルから物体座標空間における物体上の点座標を読み込みます。
 *
 * @param[in] filename ファイル名
 * @param[out] objectPoints 物体上の点
 * @return 読み込めた場合はtrue、そうでなければfalse
 */
static bool readObjectPoints(const std::string& filename,
        std::vector<cv::Point3f>& objectPoints) {
    std::ifstream ifs(filename.c_str());
    if (!ifs.is_open()) {
        std::cerr << "ERROR: Failed to open file: " << filename << std::endl;
        return false;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        int x, y, z;
        sscanf(line.c_str(), "%d,%d,%d", &x, &y, &z);
        objectPoints.push_back(cv::Point3f(x, y, z));
    }
    return true;
}

static void onMouse(int event, int x, int y, int flags, void* params) {
    switch (event) {
        case cv::EVENT_LBUTTONDOWN:
            assert(s_clickedPoints != NULL);

            if (s_clickedPoints->size() >= s_maxClickedCount) {
                return;
            }
            cv::Point2f point(x, y);
            s_clickedPoints->push_back(point);
            std::cout << "count=" << s_clickedPoints->size() << ", clicked=" << point << std::endl;
            drawCross(s_shownImage, point, cv::Scalar(0, 0, 255), 7, 2);
            cv::imshow(s_shownWindowName, s_shownImage);
            break;
    }
}

/**
 * 画像から画像上の対応点を読み込みます。
 *
 * @param[in] filename 画像ファイル名
 * @param[in] numPoints 対応点の数
 * @param[out] imagePoints 画像上の対応点
 * @return 読み込めた場合はtrue、そうでなければfalse
 */
static bool readImagePoints(const std::string& filename,
        int numPoints,
        std::vector<cv::Point2f>& imagePoints) {
    s_shownWindowName = filename;
    s_shownImage = cv::imread(filename);
    if (s_shownImage.data == NULL) {
        std::cerr << "ERROR: Failed to read image" << filename << std::endl;
        return false;
    }
    s_maxClickedCount = numPoints;
    s_clickedPoints = &imagePoints;

    cv::namedWindow(s_shownWindowName, cv::WINDOW_AUTOSIZE);
    cv::imshow(s_shownWindowName, s_shownImage);
    cv::setMouseCallback(s_shownWindowName, onMouse);
    cv::waitKey(0);
    if (s_clickedPoints->size() < s_maxClickedCount) {
        return false;
    }

    std::cout << "\nclickedImagePoints:\n" << imagePoints << std::endl;
    s_clickedPoints = NULL;
    return true;
}

/**
 * ファイルからカメラパラメータを読み込みます。
 *
 * @param[in] filename ファイル名
 * @param[out] intrinsic カメラの内部パラメータ行列
 * @param[out] distortion 歪み係数ベクトル
 * @return 読み込めた場合はtrue、そうでなければfalse
 */
static bool readCameraParameters(const std::string& filename,
        cv::Mat& intrinsic, cv::Mat& distortion) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "ERROR: Failed to open file: " << filename << std::endl;
        return false;
    }
    fs["intrinsic"] >> intrinsic;
    fs["distortion"] >> distortion;
    fs.release();
    return intrinsic.total() != 0 && distortion.total() != 0;
}

/**
 * 手動で入力した画像上の対応点と画像に再投影した画像上の対応点を比較します。
 *
 * @param[in] points 手動で入力した画像上の対応点
 * @param[in] reprojectedPoints 再投影した画像上の対応点
 */
static void evaluateImagePoints(const std::vector<cv::Point2f>& points,
        const std::vector<cv::Point2f>& reprojectedPoints) {
    for (std::vector<cv::Point2f>::const_iterator it = points.begin(); it != points.end(); it++) {
        drawCross(s_shownImage, *it, cv::Scalar(0, 0, 255), 7, 2);
    }
    for (std::vector<cv::Point2f>::const_iterator it = reprojectedPoints.begin(); it != reprojectedPoints.end(); it++) {
        drawCross(s_shownImage, *it, cv::Scalar(255, 0, 0), 7, 2);
    }
    cv::imshow(s_shownWindowName, s_shownImage);
    cv::waitKey(0);
    cv::destroyWindow(s_shownWindowName);

    std::cout << "reprojectedImagePoints:\n" << reprojectedPoints << "\n\n";
}

/**
 * 物体座標空間におけるカメラ位置を推定します。
 *
 * @param[in] objectPointsFileName 物体上の点座標が書かれたファイル名
 * @param[in] imageFileName 対応する物体が写っている画像ファイル名
 * @param[in] cameraParamsFileName カメラの内部パラメータが書かれたファイル名
 * @param[out] rvec カメラの回転ベクトル
 * @param[out] tvec カメラの並進ベクトル
 * @return 推定できた場合はtrue、そうでなければfalse
 */
static bool estimateCameraPosition(const std::string& objectPointsFileName,
        const std::string& imageFileName,
        const std::string& cameraParamsFileName,
        cv::Mat& rvec,
        cv::Mat& tvec) {
    std::vector<cv::Point3f> objectPoints;
    if (!readObjectPoints(objectPointsFileName, objectPoints)) {
        std::cerr << "ERROR: Failed to read object points\n";
        return false;
    }

    std::vector<cv::Point2f> imagePoints;
    if (!readImagePoints(imageFileName, objectPoints.size(), imagePoints)) {
        std::cerr << "ERROR: Failed to read image points\n";
        return false;
    }

    cv::Mat intrinsic, distortion;
    if (!readCameraParameters(cameraParamsFileName, intrinsic, distortion)) {
        std::cerr << "ERROR: Failed to read camera parameters\n";
        return false;
    }

    cv::solvePnP(objectPoints, imagePoints, intrinsic, distortion, rvec, tvec);

    std::vector<cv::Point2f> reprojectedImagePoints;
    cv::projectPoints(objectPoints, rvec, tvec, intrinsic, distortion, reprojectedImagePoints);
    evaluateImagePoints(imagePoints, reprojectedImagePoints);
    return true;
}

/**
 * ファイルにカメラ位置を書き込みます。
 *
 * @param[in] filename ファイル名
 * @param[in] rvec カメラの回転ベクトル
 * @param[in] tvec カメラの並進ベクトル
 * @return 書き込めた場合はtrue、そうでなければfalse
 */
static bool writeCameraPosition(const std::string& filename,
        const cv::Mat& rvec, const cv::Mat& tvec) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    if (!fs.isOpened()) {
        std::cerr << "ERROR: Failed to open file: " << filename << std::endl;
        return false;
    }
    fs << "rotation" << rvec;
    fs << "translation" << tvec;
    return true;
}

int main(int argc, char** argv) {
    if (argc <= 3) {
        std::cerr << "usage: "
                << argv[0]
                << " <object points file> <image file> <camera parameters file>"
                << std::endl;
        return 1;
    }
    const std::string objectPointsFileName(argv[1]);
    const std::string imageFileName(argv[2]);
    const std::string cameraParamsFileName(argv[3]);

    cv::Mat rvec, tvec;
    if(!estimateCameraPosition(objectPointsFileName, imageFileName, cameraParamsFileName,
            rvec, tvec)) {
        std::cerr << "ERROR: Failed to estimate camera position" << std::endl;
        return 1;
    }
    std::cout << "rvec:\n" << rvec << std::endl;
    std::cout << "tvec:\n" << tvec << std::endl;

    const std::string cameraPositionFileName = "camera_position.xml";
    if (writeCameraPosition(cameraPositionFileName, rvec, tvec)) {
        std::cout << "Write the camera position to " << cameraPositionFileName << std::endl;
    }
    return 0;
}
