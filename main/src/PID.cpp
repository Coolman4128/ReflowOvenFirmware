#include "PID.hpp"
#include <algorithm>

double PID::Calculate(double setPoint, double processValue) {
    // Placeholder implementation, replace with actual PID calculation
    return std::clamp(5.0 * (setPoint - processValue), -100.0, 100.0); // Simple proportional control for demonstration

}