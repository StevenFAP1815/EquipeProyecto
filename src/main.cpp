#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

int main() {
    std::cout << "Simulation has started..." << std::endl;

    cv::Mat canvas = cv::Mat::zeros(600, 800, CV_8UC3);
    cv::putText(canvas,
                "Equipetrol Delivery - toolchain OK",
                cv::Point(40, 300),
                cv::FONT_HERSHEY_SIMPLEX,
                1.0,
                cv::Scalar(255, 255, 255),
                2);

    cv::namedWindow("Equipetrol Delivery", cv::WINDOW_AUTOSIZE);
    cv::imshow("Equipetrol Delivery", canvas);

    std::cout << "Press any key in the window to exit..." << std::endl;
    cv::waitKey(0);

    cv::destroyAllWindows();
    std::cout << "Clean exit." << std::endl;
    return 0;
}