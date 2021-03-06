/*
 * Copyright (C) 2017 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include "UnicycleController.h"
#include "iDynTree/Core/EigenHelpers.h"
#include "Eigen/Core"
#include <iostream>
#include <cmath>
#include <algorithm>

double UnicyleController::saturate(double input, double saturation)
{
    if (saturation < 0)
        return input;

    if (input > saturation)
        return saturation;
    else if (input < (-saturation))
        return -saturation;
    else return input;
}

void UnicyleController::interpolateReferences(double time,
                                              const std::deque<TrajectoryPoint>::reverse_iterator &point,
                                              iDynTree::Vector2 &yOutput, iDynTree::Vector2 &yDotOutput)
{
    //actually point-1 is the next point, we are using reverse pointers
    double ratio = ((point-1)->initTime - time)/((point-1)->initTime - point->initTime);

    iDynTree::toEigen(yOutput) = ratio * iDynTree::toEigen(point->yDesired) + (1-ratio) * iDynTree::toEigen((point-1)->yDesired);
    iDynTree::toEigen(yDotOutput) = ratio * iDynTree::toEigen(point->yDotDesired) + (1-ratio) * iDynTree::toEigen((point-1)->yDotDesired);

}

UnicyleController::UnicyleController()
    :iDynTree::optimalcontrol::Controller(2)
    ,m_theta(0)
    ,m_gain(10)
    ,m_maxVelocity(-1) //negative value -> don't saturate
    ,m_maxAngularVelocity(-1) //negative value -> don't saturate
    ,m_time(0)
    ,m_slowWhenTurnGain(0.0)
{
    m_personDistance(0) = 0.2;
    m_personDistance(1) = 0.0;
    m_personPosition.zero();

    m_y.zero();

    m_inverseB.resize(2,2);
    m_inverseB.zero();

    m_R.resize(2,2);
    m_R.zero();

    TrajectoryPoint dummyReference;
    dummyReference.initTime = 0;
    dummyReference.yDesired.zero();
    dummyReference.yDotDesired.zero();
    m_desiredTrajectory.push_back(dummyReference);

}

bool UnicyleController::doControl(iDynTree::VectorDynSize &controllerOutput)
{
    if(controllerOutput.size() != this->controlSpaceSize())
        controllerOutput.resize(this->controlSpaceSize());

    double s_theta = std::sin(m_theta);
    double c_theta =std::cos(m_theta);
    double dx = m_personDistance(0);
    double dy = m_personDistance(1);

    m_inverseB(0,0) = -dy/dx*s_theta + c_theta;
    m_inverseB(0,1) = dy/dx*c_theta + s_theta;
    m_inverseB(1,0) = -s_theta/dx;
    m_inverseB(1,1) = c_theta/dx;

    iDynTree::Vector2 yDesired, yDotDesired;

    if (!getDesiredPoint(m_time, yDesired, yDotDesired)){
        std::cerr << "Error while reading the desired point." << std::endl;
        return false;
    }

    iDynTree::toEigen(controllerOutput) = iDynTree::toEigen(m_inverseB) * (iDynTree::toEigen(yDotDesired) -
                                          m_gain * (iDynTree::toEigen(m_y) - iDynTree::toEigen(yDesired)));

    controllerOutput(1) = saturate(controllerOutput(1), m_maxAngularVelocity);
    double velocitySaturation = m_maxVelocity / (1.0 + m_slowWhenTurnGain * (std::sqrt(controllerOutput(1) * controllerOutput(1))));
    controllerOutput(0) = saturate(controllerOutput(0), velocitySaturation);

    return true;
}

bool UnicyleController::setStateFeedback(const double t, const iDynTree::VectorDynSize &stateFeedback){
    if (stateFeedback.size() != 3)
        return false;
    m_time = t;

    m_theta = stateFeedback(2);

    iDynTree::Vector2 unicyclePosition;
    unicyclePosition(0) = stateFeedback(0);
    unicyclePosition(1) = stateFeedback(1);

    m_y = getPersonPosition(unicyclePosition, m_theta);

    return true;
}

bool UnicyleController::setPersonDistance(double xPosition, double yPosition)
{
    if(xPosition == 0){
        std::cerr << "The xPosition need to be greater than 0." << std::endl;
        return false;
    }
    m_personDistance(0) = xPosition;
    m_personDistance(1) = yPosition;
    return true;
}

const iDynTree::Vector2 &UnicyleController::getPersonDistance() const
{
    return m_personDistance;
}

const iDynTree::Vector2 &UnicyleController::getPersonPosition(const iDynTree::Vector2 &unicyclePosition, double unicycleAngle)
{
    double c_theta = std::cos(unicycleAngle);
    double s_theta = std::sin(unicycleAngle);

    m_R(0,0) = c_theta;
    m_R(0,1) = -s_theta;
    m_R(1,0) = s_theta;
    m_R(1,1) = c_theta;

    iDynTree::toEigen(m_personPosition) = iDynTree::toEigen(unicyclePosition) + iDynTree::toEigen(m_R)*iDynTree::toEigen(m_personDistance);

    return m_personPosition;
}

bool UnicyleController::setGain(double controllerGain)
{
    if (controllerGain <= 0){
        std::cerr << "The controller gain is supposed to be positive." << std::endl;
        return false;
    }
    m_gain = controllerGain;
    return true;
}

bool UnicyleController::setSaturations(double maxVelocity, double maxAngularVelocity)
{
    if ((maxVelocity < 0)||(maxAngularVelocity < 0)){
        std::cerr << "The saturations are on the absolute value, thus they need to be non-negative." << std::endl;
        return false;
    }

    m_maxVelocity = maxVelocity;
    m_maxAngularVelocity = maxAngularVelocity;

    return true;
}

bool UnicyleController::setSlowWhenTurnGain(double slowWhenTurnGain)
{
    if (slowWhenTurnGain < 0){
        std::cerr << "The slowWhenTurn gain is supposed to be non-negative." << std::endl;
        return false;
    }

    m_slowWhenTurnGain = slowWhenTurnGain;
    return true;
}

bool UnicyleController::setDesiredPoint(const TrajectoryPoint &desiredPoint)
{
    if (desiredPoint.initTime < 0){
        std::cerr << "The initTime parameter is supposed to be non-negative." << std::endl;
        return false;
    }

    m_desiredTrajectory.push_back(desiredPoint);

    std::sort(m_desiredTrajectory.begin(), m_desiredTrajectory.end(), [](const TrajectoryPoint &a, const TrajectoryPoint &b) { return a.initTime < b.initTime;}); //reorder the vector

    return true;
}

bool UnicyleController::getDesiredTrajectoryInitialTime(double &firstTime)
{
    if (m_desiredTrajectory.empty()){
        std::cerr << "No trajectory loaded yet." << std::endl;
        return false;
    }

    firstTime = m_desiredTrajectory.front().initTime;
    return true;
}

void UnicyleController::clearDesiredTrajectory()
{
    m_desiredTrajectory.clear();
}

bool UnicyleController::clearDesiredTrajectoryUpTo(double time)
{
    if (time < 0){
        std::cerr << "The time is expected to be non-negative" <<std::endl;
        return false;
    }

    if (m_desiredTrajectory.empty()){
        return true;
    }

    iDynTree::Vector2 yDesired, yDotDesired;
    if(!getDesiredPoint(time, yDesired, yDotDesired))
        return false;

    std::deque<TrajectoryPoint>::iterator element = m_desiredTrajectory.begin();

    while((element != m_desiredTrajectory.end()) && (element->initTime <= time)){
        element++;
        m_desiredTrajectory.pop_front();
    }
    TrajectoryPoint firstPoint;
    firstPoint.yDesired = yDesired;
    firstPoint.yDotDesired = yDotDesired;
    firstPoint.initTime = time;
    m_desiredTrajectory.push_front(firstPoint);

    return true;
}

bool UnicyleController::getDesiredPoint(double time, iDynTree::Vector2 &yDesired, iDynTree::Vector2 &yDotDesired)
{
    if (time < 0){
        std::cerr << "The time is expected to be non-negative" <<std::endl;
        return false;
    }

    if (m_desiredTrajectory.empty()){
        std::cerr << "First you have to load a desired trajectory." <<std::endl;
        return false;
    }

    double initTime;
    getDesiredTrajectoryInitialTime(initTime);

    if (time < initTime){
        std::cerr << "The time is before the beginning of the desired trajectory." << std::endl;
        return false;
    }

    std::deque<TrajectoryPoint>::reverse_iterator pointIterator =
            std::find_if(m_desiredTrajectory.rbegin(),
                         m_desiredTrajectory.rend(),
                         [time](const TrajectoryPoint & a) -> bool { return a.initTime <= time; }); //find the last element in the vector with init time lower than the specified time
    if (pointIterator == m_desiredTrajectory.rend()){ //something went wrong
        std::cerr << "Something went wrong while getting the reference." <<std::endl;
        return false;
    }
    if (pointIterator == m_desiredTrajectory.rbegin()){
        yDesired = pointIterator->yDesired;
        yDotDesired = pointIterator->yDotDesired;
    } else
        interpolateReferences(time, pointIterator, yDesired, yDotDesired);

    return true;
}
