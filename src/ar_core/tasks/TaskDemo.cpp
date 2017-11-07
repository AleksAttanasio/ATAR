//
// Created by nima on 13/06/17.
//

#include "TaskDemo.h"
#include <custom_conversions/Conversions.h>
#include <boost/thread/thread.hpp>

TaskDemo::TaskDemo(ros::NodeHandlePtr n)
        :
        SimTask(n, 500) ,
        time_last(ros::Time::now())
{

    bool ar_mode = false;
    int n_views = 1;
    bool one_window_per_view = false;
    bool borders_off  = false;
    std::vector<int> view_resolution = {920, 640};
    std::vector<int> window_positions={100,50, 800, 50};
    // the only needed argument to construct a Renderer if the nodehandle ptr
    // The rest have default values.
    graphics = new Rendering(n, view_resolution, ar_mode, n_views,
                             one_window_per_view, borders_off,window_positions);

    // Define a master manipulator
    master = new Manipulator(nh, "/sigma7/sigma0", "/pose", "/gripper_angle");

    // for correct calibration, the master needs the pose of the camera
    graphics->SetManipulatorInterestedInCamPose(master);

   

    // DEFINE OBJECTS
    // -------------------------------------------------------------------------
    // static floor
    // always add a floor under the workspace of your task to prevent objects
    // from falling too far and mess things up.
    std::vector<double> floor_dims = {0., 0., 1., -0.5};
    SimObject *floor = new SimObject(ObjectShape::STATICPLANE,
                                     ObjectType::DYNAMIC, floor_dims);
    dynamics_world->addRigidBody(floor->GetBody());

    // -------------------------------------------------------------------------
    // Create a floor
    SimObject *board;
    {
        // define object dimensions
        std::vector<double> board_dimensions = {0.15, 0.10, 0.01};

        // define object Pose
        KDL::Frame pose(KDL::Rotation::Quaternion( 0., 0., 0., 1.)
                , KDL::Vector( board_dimensions[0] / 3,
                               board_dimensions[1] / 2,
                               -board_dimensions[2]/ 2) );

        // we want a static floor so ObjectType is DYNAMIC and no density is
        // passed (default is zero)
        board = new SimObject(ObjectShape::BOX, ObjectType::DYNAMIC,
                              board_dimensions, pose);

        // we can access all the properties of a VTK actor
        board->GetActor()->GetProperty()->SetColor(colors.Gray);

        // we need to add the rigid body to the dynamics workd and the actor
        // to the graphics_actors vector
        dynamics_world->addRigidBody(board->GetBody());
        graphics_actors.push_back(board->GetActor());
    }
    // -------------------------------------------------------------------------
    // Create 6 dynamic spheres
    {
        // define object dimensions
        std::vector<double> sphere_dimensions = {0.005};

        // define object Pose
        KDL::Frame pose(KDL::Rotation::Quaternion( 0., 0., 0., 1.)
                , KDL::Vector(0.02, 0.05, 0.05 ));

        // Define
        double density = 50000; // kg/m3
        for (int i = 0; i < 6; ++i) {

            pose.p = pose.p+ KDL::Vector(0.0001, 0.0, 0.008);

            sphere[i] = new SimObject(ObjectShape::SPHERE, ObjectType::DYNAMIC,
                                      sphere_dimensions, pose, density);

            // we can access all the properties of a VTK actor
            sphere[i]->GetActor()->GetProperty()->SetColor(colors.BlueDodger);
            // same applies for bullet parameters
            sphere[i]->GetBody()->setFriction(1.5f);
            sphere[i]->GetBody()->setRollingFriction(0.5f);
            sphere[i]->GetBody()->setSpinningFriction(0.5f);

            // we need to add the rigid body to the dynamics workd and the actor
            // to the graphics_actors vector
            dynamics_world->addRigidBody(sphere[i]->GetBody());
            graphics_actors.push_back(sphere[i]->GetActor());
        }

    }

    // -------------------------------------------------------------------------
    // Create 6 dynamic cubes
    // objects can be defined locally too...
    {
        // define object dimensions
        std::vector<double> sphere_dimensions = {0.01, 0.01, 0.005};

        // define object Pose
        KDL::Frame pose(KDL::Rotation::Quaternion( 0., 0., 0., 1.)
                , KDL::Vector(0.05, 0.055, 0.05 ));

        // Define
        double density = 50000; // kg/m3
        //override the default friction value
        float friction = 0.2;

        for (int i = 0; i < 6; ++i) {

            pose.p = pose.p+ KDL::Vector(0.0001, 0.0, 0.008);

            SimObject cube(ObjectShape::BOX, ObjectType::DYNAMIC,
                           sphere_dimensions, pose, density, friction);

            // we can access all the properties of a VTK actor
            cube.GetActor()->GetProperty()->SetColor(colors.Orange);

            // we need to add the rigid body to the dynamics workd and the actor
            // to the graphics_actors vector
            dynamics_world->addRigidBody(cube.GetBody());
            graphics_actors.push_back(cube.GetActor());
        }

    }
    // -------------------------------------------------------------------------
    // Create Forceps
    {
        KDL::Frame forceps_pose = KDL::Frame(KDL::Vector(0.05, 0.11, 0.08));
        forceps_pose.M.DoRotZ(M_PI/2);
        forceps = new Forceps(MESH_DIRECTORY, forceps_pose);
        forceps->AddToWorld(dynamics_world);
        forceps->AddToActorsVector(graphics_actors);

    }

    //    master = new Manipulator(nh, "/dvrk/MTML",
    //                                   "/position_cartesian_current",
    //                                   "/gripper_position_current",
    //                                   cam_pose);
    graphics->AddActorsToScene(GetActors());

};

//------------------------------------------------------------------------------
void TaskDemo::StepWorld() {


    KDL::Frame cam_p = graphics->GetMainCameraPose();
    cam_p.M.DoRotZ(-0.005);
    graphics->SetMainCameraPose(cam_p);

    graphics->Render();

    
    //--------------------------------
    //use the tool pose for moving the virtual tools ...
    //    KDL::Frame tool_pose = (*tool_current_pose_kdl[0]);
    forceps->SetPoseAndJawAngle(tool_pose, grip_angle);

    //--------------------------------
    // step the world
    StepPhysics();

    // you can access the pose of the objects:
    //    ROS_INFO("Sphere0 z: %f",sphere[0]->GetPose().p[2]);

}


custom_msgs::TaskState TaskDemo::GetTaskStateMsg() {
    custom_msgs::TaskState task_state_msg;
    return task_state_msg;
}

void TaskDemo::ResetTask() {

    // save the measurements and the metrics and reset the initial conditions
    // to start a new repetition of the task
    ROS_INFO("Repetition completed. Resetting the task.");

    // save metrics


}

void TaskDemo::ResetCurrentAcquisition() {
    ROS_INFO("Resetting current acquisition.");
}


void TaskDemo::HapticsThread() {

    ros::Publisher pub_desired[2];

    ros::NodeHandlePtr node = boost::make_shared<ros::NodeHandle>();
    pub_desired[0] = node->advertise<geometry_msgs::PoseStamped>
            ("PSM1/tool_pose_desired", 10);
//    if(bimanual)
//        pub_desired[1] = node->advertise<geometry_msgs::PoseStamped>
//                ("/PSM2/tool_pose_desired", 10);

    ros::Rate loop_rate(200);

    while (ros::ok())
    {
        // read the master pose and gripper angle
        master->GetPoseWorld(tool_pose);
        master->GetGripper(grip_angle);

        //        CalculatedDesiredToolPose();

//        // publish desired poses
//        for (int n_arm = 0; n_arm < 1+ int(bimanual); ++n_arm) {
//
//            // convert to pose message
//            geometry_msgs::PoseStamped pose_msg;
//            tf::poseKDLToMsg(tool_desired_pose_kdl[n_arm], pose_msg.pose);
//            // fill the header
//            pose_msg.header.frame_id = "/task_space";
//            pose_msg.header.stamp = ros::Time::now();
//            // publish
//            pub_desired[n_arm].publish(pose_msg);
//        }

        ros::spinOnce();
        loop_rate.sleep();
        boost::this_thread::interruption_point();
    }
}


void TaskDemo::StepPhysics() {

    double time_step = (ros::Time::now() - time_last).toSec();
    //std::cout << "time_step: " << time_step << std::endl;
    dynamics_world->stepSimulation(btScalar(time_step), 100, 1.f/128.f);
    time_last = ros::Time::now();

}

// -----------------------------------------------------------------------------
void TaskDemo::StartManipulatorToWorldFrameCalibration(const uint arm_id) {

    master->DoArmToWorldFrameCalibration();
    // TODO : Update the implementation of this
//    ROS_INFO("Starting Arm 1 to world calibration->");
//    if(running_task_id>0) {
//        // if a task is running first stop it
//        graphics->RemoveAllActorsFromScene();
//        DeleteTask();
//        // then do the calibration
//        DoArmToWorldFrameCalibration(arm_id);
//        // run the task again
//        StartTask((uint)running_task_id);
//        graphics->AddActorsToScene(task_ptr->GetActors());
//    }
//    else // if no task is running just do the calibration
//        DoArmToWorldFrameCalibration(arm_id);
//
//    control_event = (int8_t)running_task_id;
}

TaskDemo::~TaskDemo() {

    ROS_INFO("Destructing task. Num bullet objects: %d",
             dynamics_world->getNumCollisionObjects());
    //remove the rigidbodies from the dynamics world and delete them
    for (int i = dynamics_world->getNumCollisionObjects() - 1; i >= 0; i--)
    {
        btCollisionObject* obj = dynamics_world->getCollisionObjectArray()[i];
        btRigidBody* body = btRigidBody::upcast(obj);
        if (body && body->getMotionState())
        {
            delete body->getMotionState();
        }
        dynamics_world->removeCollisionObject(obj);
        delete obj;
    }

    //delete dynamics world
    delete dynamics_world;

    //delete solver
    delete solver;

    //delete broadphase
    delete overlappingPairCache;

    //delete dispatcher
    delete dispatcher;

    delete collisionConfiguration;

    delete master;

    delete graphics;


}


