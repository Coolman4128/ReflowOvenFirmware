#pragma once

class PID{
    public:
        PID() = default;
        double Calculate(double setPoint, double processValue);

    private:


};