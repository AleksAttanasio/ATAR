//
// Created by nima on 4/18/17.
//

#include <custom_conversions/Conversions.h>
#include <boost/thread/thread.hpp>
#include <geometry_msgs/PoseStamped.h>
#include "TaskBuzzWire.h"
#include "src/ar_core/Colors.hpp"
#include "src/ar_core/VTKConversions.h"
#include <vtkCubeSource.h>

TaskBuzzWire::TaskBuzzWire(const std::string stl_file_dir, const bool biman,
                           const bool with_guidance, const double haptic_loop_rate,
                           const std::string slave_names_in[],
                           KDL::Frame *slave_to_world_tr)
    :
    SimTask(NULL, haptic_loop_rate),
    stl_files_dir(stl_file_dir),
    slave_frame_to_world_frame_tr(slave_to_world_tr),
//        show_ref_frames(show_ref_frames),
    bimanual(biman),
    with_guidance(with_guidance),
    destination_ring_counter(0),
    ac_params_changed(true),
    task_state(TaskState::Idle),
    number_of_repetition(0),
    n_score_history(10)
{


    slave_names = new std::string[bimanual+1];

    *slave_names = *slave_names_in;
    // -------------------------------------------------------------------------
    //  ACTIVE CONSTRAINT
    // -------------------------------------------------------------------------
    // these parameters could be set as ros parameters too but since
    // they change during the task I am hard coding them here.
    ac_parameters.method = 0; // 0 for visco/elastic
    ac_parameters.active = 0;

    ac_parameters.max_force = 4.0;
    ac_parameters.linear_elastic_coeff = 1000.0;
    ac_parameters.linear_damping_coeff = 10.0;

    ac_parameters.max_torque = 0.03;
    ac_parameters.angular_elastic_coeff = 0.04;
    ac_parameters.angular_damping_coeff = 0.002;


    // -------------------------------------------------------------------------
    //  INITIALIZING GRAPHICS ACTORS
    // -------------------------------------------------------------------------

    ring_actor[0] = vtkSmartPointer<vtkActor>::New();
    tool_current_frame_axes[0] = vtkSmartPointer<vtkAxesActor>::New();
    tool_desired_frame_axes[0] = vtkSmartPointer<vtkAxesActor>::New();
    tool_current_pose[0] = vtkSmartPointer<vtkMatrix4x4>::New();

    if(bimanual){
        ring_actor[1] = vtkSmartPointer<vtkActor>::New();
        tool_current_frame_axes[1] = vtkSmartPointer<vtkAxesActor>::New();
        tool_desired_frame_axes[1] = vtkSmartPointer<vtkAxesActor>::New();
        tool_current_pose[1] = vtkSmartPointer<vtkMatrix4x4>::New();
    }

    tube_mesh_actor = vtkSmartPointer<vtkActor>::New();

    destination_ring_actor= vtkSmartPointer<vtkActor>::New();

    cellLocator = vtkSmartPointer<vtkCellLocator>::New();

    line1_source = vtkSmartPointer<vtkLineSource>::New();

    line2_source = vtkSmartPointer<vtkLineSource>::New();

    line1_actor = vtkSmartPointer<vtkActor>::New();

    line2_actor = vtkSmartPointer<vtkActor>::New();

    // -------------------------------------------------------------------------
    // TOOL RINGS
    ring_radius = 0.004;
    double ring_cross_section_radius = 0.0005;
    double source_scales = 0.006;

    vtkSmartPointer<vtkParametricTorus> parametricObject =
        vtkSmartPointer<vtkParametricTorus>::New();
    parametricObject->SetCrossSectionRadius(
        ring_cross_section_radius / source_scales);
    parametricObject->SetRingRadius(ring_radius / source_scales);

    vtkSmartPointer<vtkParametricFunctionSource> parametricFunctionSource =
        vtkSmartPointer<vtkParametricFunctionSource>::New();
    parametricFunctionSource->SetParametricFunction(parametricObject);
    parametricFunctionSource->Update();

    // to transform the data
    vtkSmartPointer<vtkTransformPolyDataFilter>
        ring_local_transform_filter[2];
    vtkSmartPointer<vtkTransform> ring_local_transform[2];
    vtkSmartPointer<vtkPolyDataMapper> ring_mapper[2];


    for (int k = 0; k < 1 + (int)bimanual; ++k) {

        ring_local_transform_filter[k] =
            vtkSmartPointer<vtkTransformPolyDataFilter>::New();
        ring_local_transform_filter[k]->SetInputConnection(
            parametricFunctionSource->GetOutputPort());

        ring_local_transform[k] = vtkSmartPointer<vtkTransform>::New();

        if(k == 0){
            ring_local_transform[k]->RotateX(90);
            ring_local_transform[k]->Translate(0.0, ring_radius /
                source_scales, 0.0);
        }
        else{
//            ring_local_transform[k]->RotateX(0);
            ring_local_transform[k]->Translate(0.0, ring_radius /
                source_scales, 0.0);
        }

        ring_local_transform_filter[k]->SetTransform(ring_local_transform[k]);
        ring_local_transform_filter[k]->Update();

        ring_mapper[k] = vtkSmartPointer<vtkPolyDataMapper>::New();
        ring_mapper[k]->SetInputConnection(
            ring_local_transform_filter[k]->GetOutputPort());

        ring_actor[k]->SetMapper(ring_mapper[k]);
        ring_actor[k]->SetScale(source_scales);
        ring_actor[k]->GetProperty()->SetColor(colors.Turquoise);
        ring_actor[k]->GetProperty()->SetSpecular(0.7);
    }

    // -------------------------------------------------------------------------
    // Destination ring

    vtkSmartPointer<vtkPolyDataMapper> destination_ring_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    destination_ring_mapper->SetInputConnection(
        parametricFunctionSource->GetOutputPort());
    destination_ring_actor->SetMapper(destination_ring_mapper);
    destination_ring_actor->SetScale(0.004);
    destination_ring_actor->RotateX(90);
    destination_ring_actor->RotateY(-60);
    destination_ring_actor->GetProperty()->SetColor(colors.Green);
    //destination_ring_actor->GetProperty()->SetOpacity(0.5);

    // -------------------------------------------------------------------------
    // FRAMES
    vtkSmartPointer<vtkAxesActor> task_coordinate_axes =
        vtkSmartPointer<vtkAxesActor>::New();

    task_coordinate_axes->SetXAxisLabelText("");
    task_coordinate_axes->SetYAxisLabelText("");
    task_coordinate_axes->SetZAxisLabelText("");
    task_coordinate_axes->SetTotalLength(0.01, 0.01, 0.01);
    task_coordinate_axes->SetShaftType(vtkAxesActor::CYLINDER_SHAFT);

    for (int k = 0; k < 1 + (int)bimanual; ++k) {

        tool_current_frame_axes[k]->SetXAxisLabelText("");
        tool_current_frame_axes[k]->SetYAxisLabelText("");
        tool_current_frame_axes[k]->SetZAxisLabelText("");
        tool_current_frame_axes[k]->SetTotalLength(0.007, 0.007, 0.007);
        tool_current_frame_axes[k]->SetShaftType(vtkAxesActor::CYLINDER_SHAFT);

        tool_desired_frame_axes[k]->SetXAxisLabelText("");
        tool_desired_frame_axes[k]->SetYAxisLabelText("");
        tool_desired_frame_axes[k]->SetZAxisLabelText("");
        tool_desired_frame_axes[k]->SetTotalLength(0.007, 0.007, 0.007);
        tool_desired_frame_axes[k]->SetShaftType(vtkAxesActor::CYLINDER_SHAFT);

    }

    // hard coding the position of of the destinations
    // if the base is rotated the destinations will not be valid anymore...
    KDL::Vector base_position = KDL::Vector(0.095, 0.09, 0.025);
    //
    idle_point  = base_position + KDL::Vector(-0.056, -0.034, 0.004);
    start_point  = base_position + KDL::Vector(-0.048, -0.029, 0.005);
    end_point  = base_position + KDL::Vector(-0.016, -0.017, 0.028);

    //idle_point  = base_position + KDL::Vector(0.056, -0.032, 0.004);
    //start_point  = base_position + KDL::Vector(0.048, -0.026, 0.005);
    //end_point  = base_position + KDL::Vector(0.020, -0.003, 0.028);

    // -------------------------------------------------------------------------
    // Stand MESH hq
    std::stringstream input_file_dir;
    input_file_dir << stl_files_dir << std::string("task1_4_stand.STL");

    vtkSmartPointer<vtkSTLReader> stand_mesh_reader =
        vtkSmartPointer<vtkSTLReader>::New();
    ROS_DEBUG("Loading stl file from: %s", input_file_dir.str().c_str());
    stand_mesh_reader->SetFileName(input_file_dir.str().c_str());
    stand_mesh_reader->Update();

    // transform
    vtkSmartPointer<vtkTransform> stand_transform =
        vtkSmartPointer<vtkTransform>::New();

    stand_transform->Translate(base_position[0], base_position[1],
                               base_position[2]);
    stand_transform->RotateX(180);
    stand_transform->RotateZ(150);

    vtkSmartPointer<vtkTransformPolyDataFilter> stand_mesh_transformFilter =
        vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    stand_mesh_transformFilter->SetInputConnection(
        stand_mesh_reader->GetOutputPort());
    stand_mesh_transformFilter->SetTransform(stand_transform);
    stand_mesh_transformFilter->Update();


    vtkSmartPointer<vtkPolyDataMapper> stand_mesh_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    stand_mesh_mapper->SetInputConnection(
        stand_mesh_transformFilter->GetOutputPort());

    vtkSmartPointer<vtkActor> stand_mesh_actor =
        vtkSmartPointer<vtkActor>::New();
    stand_mesh_actor->SetMapper(stand_mesh_mapper);
    stand_mesh_actor->GetProperty()->SetColor(colors.Gray);
    //    stand_mesh_actor->GetProperty()->SetSpecular(0.8);


    // -------------------------------------------------------------------------
    // MESH hq is for rendering and lq is for generating
    // active constraints
    input_file_dir.str("");
    input_file_dir << stl_files_dir << std::string("task1_4_tube.STL");

    vtkSmartPointer<vtkSTLReader> hq_mesh_reader =
        vtkSmartPointer<vtkSTLReader>::New();
    ROS_DEBUG("Loading stl file from: %s", input_file_dir.str().c_str());
    hq_mesh_reader->SetFileName(input_file_dir.str().c_str());
    hq_mesh_reader->Update();
    vtkSmartPointer<vtkTransform> tube_transform =
        vtkSmartPointer<vtkTransform>::New();
    tube_transform->DeepCopy(stand_transform);
    tube_transform->RotateX(-15);

    vtkSmartPointer<vtkTransformPolyDataFilter> hq_mesh_transformFilter =
        vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    hq_mesh_transformFilter->SetInputConnection(
        hq_mesh_reader->GetOutputPort());
    hq_mesh_transformFilter->SetTransform(tube_transform);
    hq_mesh_transformFilter->Update();

    vtkSmartPointer<vtkPolyDataMapper> hq_mesh_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    hq_mesh_mapper->SetInputConnection(
        hq_mesh_transformFilter->GetOutputPort());

    tube_mesh_actor->SetMapper(hq_mesh_mapper);
    tube_mesh_actor->GetProperty()->SetColor(colors.Orange);
    tube_mesh_actor->GetProperty()->SetSpecular(0.8);
    tube_mesh_actor->GetProperty()->SetSpecularPower(80);
        //tube_mesh_actor->GetProperty()->SetOpacity(0.5);


    // -------------------------------------------------------------------------
    // MESH lq
    input_file_dir.str("");
    input_file_dir << stl_files_dir << std::string("task1_4_wire_vhq.STL");

    vtkSmartPointer<vtkSTLReader> lq_mesh_reader =
        vtkSmartPointer<vtkSTLReader>::New();
    ROS_DEBUG("Loading stl file from: %s", input_file_dir.str().c_str());
    lq_mesh_reader->SetFileName(input_file_dir.str().c_str());
    lq_mesh_reader->Update();


    vtkSmartPointer<vtkTransformPolyDataFilter> lq_mesh_transformFilter =
        vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    lq_mesh_transformFilter->SetInputConnection(
        lq_mesh_reader->GetOutputPort());
    // TODO: just changed the wire mesh and its coordinate frame doesn't match
    // that of the tube. Fixing it here temporarily
    vtkSmartPointer<vtkTransform> wire_transform =
        vtkSmartPointer<vtkTransform>::New();
    wire_transform->DeepCopy(tube_transform);
    wire_transform->Translate(0.0, 0.0, -0.030);

    lq_mesh_transformFilter->SetTransform(wire_transform);
    lq_mesh_transformFilter->Update();

    vtkSmartPointer<vtkPolyDataMapper> lq_mesh_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    lq_mesh_mapper->SetInputConnection(
        lq_mesh_transformFilter->GetOutputPort());

    vtkSmartPointer<vtkActor> lq_mesh_actor = vtkSmartPointer<vtkActor>::New();
    lq_mesh_actor->SetMapper(lq_mesh_mapper);

    // CLOSEST POINT will be found on the low quality mesh
    cellLocator->SetDataSet(lq_mesh_transformFilter->GetOutput());
    cellLocator->BuildLocator();

    // -------------------------------------------------------------------------
    // Create a cube for the floor
    vtkSmartPointer<vtkCubeSource> floor_source =
        vtkSmartPointer<vtkCubeSource>::New();
    double floor_dimensions[3] = {0.14, 0.12, 0.001};
    floor_source->SetXLength(floor_dimensions[0]);
    floor_source->SetYLength(floor_dimensions[1]);
    floor_source->SetZLength(floor_dimensions[2]);
    vtkSmartPointer<vtkPolyDataMapper> floor_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    floor_mapper->SetInputConnection(floor_source->GetOutputPort());
    vtkSmartPointer<vtkActor> floor_actor = vtkSmartPointer<vtkActor>::New();
    floor_actor->SetMapper(floor_mapper);
    floor_actor->SetPosition(floor_dimensions[0] / 2, floor_dimensions[1] / 2,
                             -floor_dimensions[2]);
    //floor_actor->GetProperty()->SetOpacity(0.3);
    floor_actor->GetProperty()->SetColor(0.3, 0.3,0.3);

    // -------------------------------------------------------------------------
    // Lines
    vtkSmartPointer<vtkPolyDataMapper> line1_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    line1_mapper->SetInputConnection(line1_source->GetOutputPort());
    line1_actor->SetMapper(line1_mapper);
    line1_actor->GetProperty()->SetLineWidth(3);
//    line1_actor->GetProperty()->SetColor(Colors::DeepPink);
    line1_actor->GetProperty()->SetOpacity(0.8);

    vtkSmartPointer<vtkPolyDataMapper> line2_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    line2_mapper->SetInputConnection(line2_source->GetOutputPort());

    line2_actor->SetMapper(line2_mapper);
    line2_actor->GetProperty()->SetLineWidth(3);
//    line2_actor->GetProperty()->SetColor(Colors::DeepPink);
    line2_actor->GetProperty()->SetOpacity(0.8);


    // -------------------------------------------------------------------------
    // TEXTS
    cornerAnnotation =
        vtkSmartPointer<vtkCornerAnnotation>::New();
    cornerAnnotation->SetLinearFontScaleFactor( 2 );
    cornerAnnotation->SetNonlinearFontScaleFactor( 1 );
    cornerAnnotation->SetMaximumFontSize( 30 );
    //        cornerAnnotation->SetText( 0, "lower left" );
    cornerAnnotation->SetText( 1, "Scores: " );
    //        cornerAnnotation->SetText( 2, "upper left" );
    //    cornerAnnotation->GetTextProperty()->SetColor( 1, 0, 0 );



    // -------------------------------------------------------------------------
    // Error history spheres

    vtkSmartPointer<vtkSphereSource>  source =
        vtkSmartPointer<vtkSphereSource>::New();

    source->SetRadius(0.002);
    source->SetPhiResolution(15);
    source->SetThetaResolution(15);
    vtkSmartPointer<vtkPolyDataMapper> sphere_mapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    sphere_mapper->SetInputConnection(source->GetOutputPort());

    for (int i = 0; i < n_score_history; ++i) {

        vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(sphere_mapper);
        actor->GetProperty()->SetColor(colors.Gray);
        actor->SetPosition(0.09- (double)i * 0.006, 0.132 - (double)i * 0.0003,
                           0.01);
        score_sphere_actors.push_back(actor);
    }


    // -------------------------------------------------------------------------
    // Add all graphics_actors to a vector
    bool show_ref_frames =0;
    if (show_ref_frames) {
        graphics_actors.push_back(task_coordinate_axes);

        for (int k = 0; k < 1 + (int)bimanual; ++k) {
            graphics_actors.push_back(tool_current_frame_axes[k]);
            graphics_actors.push_back(tool_desired_frame_axes[k]);
        }
    }

    graphics_actors.push_back(tube_mesh_actor);
    graphics_actors.push_back(stand_mesh_actor);
    graphics_actors.push_back(lq_mesh_actor);
        graphics_actors.push_back(floor_actor);
    graphics_actors.push_back(ring_actor[0]);
    if(bimanual){
        graphics_actors.push_back(ring_actor[1]);
        graphics_actors.push_back(line1_actor);
        graphics_actors.push_back(line2_actor);
    }
    graphics_actors.push_back(destination_ring_actor);
    for (int j = 0; j < score_sphere_actors.size(); ++j) {
        graphics_actors.push_back(score_sphere_actors[j]);
    }
    //    graphics_actors.push_back(ring_guides_mesh_actor);
    //    graphics_actors.push_back(cornerAnnotation);


}

//------------------------------------------------------------------------------
//std::vector<vtkSmartPointer<vtkProp> > TaskBuzzWire::GetActors() {
//    return graphics_actors;
//}

//------------------------------------------------------------------------------
void TaskBuzzWire::StepWorld() {

    // -------------------------------------------------------------------------
    // Find closest points and update frames
    for (int k = 0; k < 1 + (int)bimanual; ++k) {

        // -------------
        // the noise in the pose measurements of the tool made the rendered
        // shadows behave unexpectedly, to fix it we do a 1 step moving
        // averqge of the position of the tool before applying it to the ring

        vtkSmartPointer<vtkMatrix4x4>   tool_pose_filt = vtkSmartPointer<vtkMatrix4x4>::New();
        KDL::Frame tool_current_kdl = *tool_current_pose_kdl[k];

        KDL::Frame tool_current_filt_kdl = tool_current_kdl;
        tool_current_filt_kdl.p = 0.5*(tool_last_pose[k].p + tool_current_kdl.p);

        VTKConversions::KDLFrameToVTKMatrix(tool_current_filt_kdl, tool_pose_filt);
        tool_last_pose[k] = *tool_current_pose_kdl[k];
        // -------------

        // setting the transformations
        tool_current_frame_axes[k]->SetUserMatrix(tool_pose_filt);
        ring_actor[k]->SetUserMatrix(tool_pose_filt);
    }

//    ring_guides_mesh_actor->SetUserMatrix(tool_current_pose[0]);


    vtkSmartPointer<vtkMatrix4x4> tool_desired_pose[2];

    for (int k = 0; k < 1 + (int)bimanual; ++k) {
        tool_desired_pose[k] =
            vtkSmartPointer<vtkMatrix4x4>::New();
        VTKConversions::KDLFrameToVTKMatrix(tool_desired_pose_kdl[k],
                                            tool_desired_pose[k]);
        tool_desired_frame_axes[k]->SetUserMatrix(tool_desired_pose[k]);
    }

    // -------------------------------------------------------------------------
    // Task logic
    // -------------------------------------------------------------------------
    double positioning_tolerance = 0.003;
    KDL::Vector destination_ring_position;


    // when the active constraints is suddenly enabled the tool makes some
    // initial large movements. To prevent always having a peak at the
    // beginning of the error, data we first activate the active constraint
    // and wait till the error is small before we start the task.
    //
    // First: if the tool is placed close to the starting point while being idle
    // we enable the active constraint.
    if (task_state == TaskState::Idle && with_guidance &&
        (ring_center[0] - start_point).Norm() <
            positioning_tolerance) {
        // Make sure the active constraint is inactive
        if (ac_parameters.active == 0) {
            ac_parameters.active = 1;
            ac_params_changed = true;
        }
    }

    // then when the error is small we start the task.
    // if guidance is off, then we don't need a restriction on the error
    // sorry it is totally unreadable!
    if (task_state == TaskState::Idle
        && ( (position_error_norm[0] < 0.001 && ac_parameters.active == 1)
            ||
                (!with_guidance && (ring_center[0] - start_point).Norm() <
                    positioning_tolerance)  ))
    {
        task_state = TaskState::ToEndPoint;
        //increment the repetition number
        number_of_repetition ++;
        // save starting time
        start_time = ros::Time::now();
        // reset score related vars
        ResetOnGoingEvaluation();
    }

        // If the tool reaches the end point the user needs to go back to
        // the starting point. This counts as a separate repetition of the task
    else if (task_state == TaskState::ToEndPoint &&
        (ring_center[0] - end_point).Norm() <
            positioning_tolerance) {
        task_state = TaskState::ToStartPoint;
        //increment the repetition number
        number_of_repetition++;

        // calculate and save the score of this repetition
        CalculateAndSaveError();

        // save starting time
        start_time = ros::Time::now();
        // reset score related vars
        ResetOnGoingEvaluation();
    }
        // If the tool reaches the start point while in ToStartPoint state,
        // we can mark the task complete
    else if (task_state == TaskState::ToStartPoint &&
        (ring_center[0] - start_point).Norm() <
            positioning_tolerance) {
        task_state = TaskState::RepetitionComplete;
        // calculate and save the score of this repetition
        CalculateAndSaveError();

        ac_parameters.active = 0;
        ac_params_changed = true;
    }

        // User needs to get away from the starting point to switch to idle
        // and in case another repetition is to be performed, the user can
        // flag that by going to the starting position again
    else if (task_state == TaskState::RepetitionComplete &&
        (ring_center[0] - idle_point).Norm() <
            positioning_tolerance) {
        task_state = TaskState::Idle;
    }

    // update the position of the cone according to the state we're in.
    if (task_state == TaskState::Idle) {
        destination_ring_position = start_point;
        destination_ring_actor->GetProperty()->SetColor(colors.Green);

    } else if (task_state == TaskState::ToStartPoint) {
        destination_ring_position = start_point;
        destination_ring_actor->GetProperty()->SetColor(colors.DeepPink);

    } else if (task_state == TaskState::ToEndPoint) {
        destination_ring_position = end_point;
        destination_ring_actor->GetProperty()->SetColor(colors.DeepPink);
    } else if (task_state == TaskState::RepetitionComplete) {
        destination_ring_position = idle_point;
        destination_ring_actor->GetProperty()->SetColor(colors.Green);

    }

    // show the destination to the user
    double dt = sin(2 * M_PI * double(destination_ring_counter) / 70);
    destination_ring_counter++;
    destination_ring_actor->SetScale(0.006 + 0.001*dt);

    destination_ring_actor->SetPosition(destination_ring_position[0],
                                        destination_ring_position[1],
                                        destination_ring_position[2]);
    // -------------------------------------------------------------------------
    // Performance Metrics
    UpdateTubeColor();

    // Populate the task state message
    task_state_msg.task_name = "BuzzWire";
    task_state_msg.task_state = (uint8_t)task_state;
    task_state_msg.number_of_repetition = number_of_repetition;
    if (task_state == TaskState::ToStartPoint
        || task_state == TaskState::ToEndPoint) {

        task_state_msg.time_stamp = (ros::Time::now() - start_time).toSec();
        task_state_msg.error_field_1 = position_error_norm[0];
        task_state_msg.error_field_1 = orientation_error_norm[0];
//        if(bimanual)
//            task_state_msg.error_field_2 = position_error_norm[1];

        // calculate score to show to user
        if (bimanual) {
            posit_error_sum +=
                0.5 * (position_error_norm[0] +
                    position_error_norm[1]);
            orient_error_sum +=
                0.5 *(orientation_error_norm[0] +
                    orientation_error_norm[1]);
        }
        else {
            posit_error_sum += position_error_norm[0];
            orient_error_sum += orientation_error_norm[0];
        }

        if(posit_error_max < position_error_norm[0])
            posit_error_max = position_error_norm[0];

        sample_count++;

    } else {
        task_state_msg.time_stamp = 0.0;
        task_state_msg.error_field_1 = 0.0;
        task_state_msg.error_field_2 = 0.0;
    }


    if(bimanual) {
        // change connection lines colors according to ring1 to ring2's distance
        double rings_distance = (ring_center[1] - ring_center[0]).Norm();
        double ideal_distance = 0.007;
        double error_ratio = 3 * fabs(rings_distance - ideal_distance)
            / ideal_distance;
        if (error_ratio > 1.0)
            error_ratio = 1.0;
        line1_actor->GetProperty()->SetColor(0.9, 0.9 - 0.7 * error_ratio,
                                             0.9 - 0.7 * error_ratio);
        line2_actor->GetProperty()->SetColor(0.9, 0.9 - 0.7 * error_ratio,
                                             0.9 - 0.7 * error_ratio);
        line1_source->Update();
        line2_source->Update();
    }
}





//------------------------------------------------------------------------------
void TaskBuzzWire::CalculatedDesiredToolPose() {
    // NOTE: All the closest points are on the wire mesh

    //---------------------------------------------------------------------
    // Find the desired orientation
    // We use two vectors to estimate the tangent of the direction of the
    // wire. One is from the grip point (current tool tip) to its closest
    // point on the wire and the other is from a point on the side of the
    // ring (90 deg from the grip point). The estimated tangent is the
    // cross product of these two (after normalization). This is just a
    // quick the non-ideal approximation.
    // Note that we could have used the central point instead of the tool
    // point but that vector gets pretty small and unstable when we're
    // close to the desired pose.
    // WHen I added the second tool things got a little bit messy!
    // Since I had to rotate the sing for 90 degrees the diserd axes
    // were different for each ring and it ended up with too many
    // hardcoded transforms, I will hopefully clean this out later. TODO

    for (int k = 0; k < 1 + (int)bimanual; ++k) {

        // make a copy of the current pose
        KDL::Frame tool_current_pose= *tool_current_pose_kdl[k];

        //Find the closest cell to the grip point
        double grip_point[3] = {(tool_current_pose).p[0],
            (tool_current_pose).p[1],
            (tool_current_pose).p[2]};

        double closest_point[3] = {0.0, 0.0, 0.0};
        double closestPointDist2; //the squared distance to the closest point
        vtkIdType cell_id; //the cell id of the cell containing the closest point
        int subId;

        cellLocator->Update();
        cellLocator->FindClosestPoint(grip_point, closest_point, cell_id, subId,
                                      closestPointDist2);
        closest_point_to_grip_point[k] = KDL::Vector(closest_point[0],
                                                     closest_point[1],
                                                     closest_point[2]);


        //Find the closest cell to the the central point
        double ring_central_point[3] = {ring_center[k][0],
            ring_center[k][1],
            ring_center[k][2]};
        cellLocator->Update();
        cellLocator->FindClosestPoint(ring_central_point, closest_point,
                                      cell_id,
                                      subId, closestPointDist2);
        closest_point_to_ring_center[k] = KDL::Vector(closest_point[0],
                                                      closest_point[1],
                                                      closest_point[2]);

        //Find the closest cell to the radial tool point
        KDL::Vector radial_tool_point_kdl;
        if(k==0)
            radial_tool_point_kdl =
                tool_current_pose *
                    KDL::Vector(ring_radius, 0.0, ring_radius);
        else
            radial_tool_point_kdl =
                tool_current_pose *
                    KDL::Vector(ring_radius, ring_radius, 0.0);

        double radial_tool_point[3] = {radial_tool_point_kdl[0],
            radial_tool_point_kdl[1],
            radial_tool_point_kdl[2]};

        cellLocator->Update();
        cellLocator->FindClosestPoint(radial_tool_point, closest_point, cell_id,
                                      subId, closestPointDist2);
        closest_point_to_radial_point[k] = KDL::Vector(closest_point[0],
                                                       closest_point[1],
                                                       closest_point[2]);


        // Find the vector from ring center to the corresponding closest point on
        // the wire
        KDL::Vector ring_center_to_cp =
            closest_point_to_ring_center[k] - ring_center[k];

        // desired pose only when the ring is close to the wire.if it is too
        // far we don't want fixtures
        if (ring_center_to_cp.Norm() < 5 * ring_radius) {


            // Desired position is one that puts the center of the wire on the
            // center of the ring.

            //---------------------------------------------------------------------
            // Find the desired position
            KDL::Vector wire_center = ring_center[k] + ring_center_to_cp;
            // Turns out trying to estimate the center of the wire makes things
            // worse so I removed the following term that was used in finding
            // wire_center:
            // wire_radius_ * (ring_center_to_cp/ring_center_to_cp.Norm());

            tool_desired_pose_kdl[k].p =
                (tool_current_pose).p + wire_center -
                    ring_center[k];



            KDL::Vector radial_to_cp =
                closest_point_to_radial_point[k] - radial_tool_point_kdl;

            KDL::Vector desired_z, desired_y, desired_x;

            KDL::Vector grip_to_cp =
                closest_point_to_grip_point[k] -
                    (tool_current_pose).p;
            if(k==0) {
                desired_z = grip_to_cp / grip_to_cp.Norm();
                desired_x = -radial_to_cp / radial_to_cp.Norm();
                desired_y = desired_z * desired_x;

                // make sure axes are perpendicular and normal
                desired_y = desired_y / desired_y.Norm();
                desired_x = desired_y * desired_z;
                desired_x = desired_x / desired_x.Norm();
                desired_z = desired_x * desired_y;
                desired_z = desired_z / desired_z.Norm();
            }
            else {
                desired_y = grip_to_cp / grip_to_cp.Norm();
                desired_x = -radial_to_cp / radial_to_cp.Norm();
                desired_z = desired_x * desired_y;

                // make sure axes are perpendicular and normal
                desired_z = desired_z / desired_z.Norm();
                desired_x = desired_y * desired_z;
                desired_x = desired_x / desired_x.Norm();
                desired_y = desired_z * desired_x;
                desired_y = desired_y / desired_y.Norm();
            }
            tool_desired_pose_kdl[k].M = KDL::Rotation(desired_x, desired_y,
                                                       desired_z);

            //------------------------------------------------------------------
            // Calculate errors
            position_error_norm[k] = (wire_center - ring_center[k]).Norm();
            KDL::Vector rpy;
            (tool_desired_pose_kdl[k].M *
                (tool_current_pose).M.Inverse() ).GetRPY(rpy[0],
                                                         rpy[1],
                                                         rpy[2]);
            orientation_error_norm[k] = rpy.Norm();


        } else {
            tool_desired_pose_kdl[k] = tool_current_pose;
            // due to the delay in teleop loop this will create some wrneches if
            // the guidance is still active
        }


        // draw the connection lines in bimanual case
        if(bimanual) {
            KDL::Vector distal_tool_point_kdl;
            if (k == 0)
                distal_tool_point_kdl =
                    tool_current_pose *
                        KDL::Vector(0.0, 0.0, 2 * ring_radius);
            else
                distal_tool_point_kdl =
                    tool_current_pose *
                        KDL::Vector(0.0, 2 * ring_radius, 0.0);

            if (k == 0) {
                line1_source->SetPoint1(grip_point);
                line2_source->SetPoint1(distal_tool_point_kdl[0],
                                        distal_tool_point_kdl[1],
                                        distal_tool_point_kdl[2]);

            } else {
                line1_source->SetPoint2(grip_point);
                line2_source->SetPoint2(distal_tool_point_kdl[0],
                                        distal_tool_point_kdl[1],
                                        distal_tool_point_kdl[2]);
            }
        }

    }




}


//------------------------------------------------------------------------------
void TaskBuzzWire::UpdateTubeColor() {

    double max_pos_error = 0.002;
    double max_orient_error = 0.3;
    // orientation error is tricky to perceive, so we weigh it half the
    // position error
    double error_ratio = ( (orientation_error_norm[0] / max_orient_error)
        + 2* (position_error_norm[0] / max_pos_error)) /3;

    if (error_ratio > 1.3)
        error_ratio = 1.3;
    else if(error_ratio < 0.3)
        error_ratio = 0.3;

//    score_sphere_actors->GetProperty()->SetColor(error_ratio, 1 - error_ratio,
//                                                0.1);
    if(task_state== TaskState::ToEndPoint
        || task_state== TaskState::ToStartPoint)
        tube_mesh_actor->GetProperty()->SetColor(0.9,
                                                 0.5- 0.4*(error_ratio-0.3),
                                                 0.1);

}

custom_msgs::TaskState TaskBuzzWire::GetTaskStateMsg() {
    return task_state_msg;
}

void TaskBuzzWire::ResetTask() {
    ROS_INFO("Resetting the task.");
    number_of_repetition = 0;
    task_state = TaskState::RepetitionComplete;
    ResetOnGoingEvaluation();
    ResetScoreHistory();
}

void TaskBuzzWire::ResetCurrentAcquisition() {
    ROS_INFO("Resetting current acquisition.");
    if(task_state== TaskState::ToEndPoint ||
        task_state == TaskState::ToStartPoint){

        ResetOnGoingEvaluation();
        if(number_of_repetition>0)
            number_of_repetition--;
        task_state = TaskState::RepetitionComplete;

    }
}


void TaskBuzzWire::HapticsThread() {

    //----------------------------------------------
    // setting  up haptics

    //std::string master_names[n_arms];
    std::string check_topic_name;

    //getting the name of the arms
    std::stringstream param_name;
    param_name << std::string("/atar/") << slave_names[0] <<
               "/tool_pose_desired";

    ros::Publisher pub_desired[2];
    ros::Publisher pub_wrench_abs[2];

    ros::NodeHandlePtr node = boost::make_shared<ros::NodeHandle>();
    pub_desired[0] = node->advertise<geometry_msgs::PoseStamped>
                             (param_name.str(), 10);
    ROS_INFO("Will publish on %s", param_name.str().c_str());

    // make sure the masters are in wrench absolute orientation
   // assuming MTMR is always used
    std::string master_topic = "/dvrk/MTMR/set_wrench_body_orientation_absolute";
    pub_wrench_abs[0] = node->advertise<std_msgs::Bool>(master_topic.c_str(), 1);
    std_msgs::Bool wrench_body_orientation_absolute;
    wrench_body_orientation_absolute.data = 1;
    pub_wrench_abs[0].publish(wrench_body_orientation_absolute);
    ROS_INFO("Setting wrench_body_orientation_absolute on %s", master_topic.c_str());

    if(bimanual) {
        //getting the name of the arms
        param_name.str("");
        param_name << std::string("/atar/") << slave_names[1] <<
                   "/tool_pose_desired";;
        pub_desired[1] = node->advertise<geometry_msgs::PoseStamped>
                                 (param_name.str(), 10);
        ROS_INFO("Will publish on %s", param_name.str().c_str());

        // make sure the masters are in wrench absolute orientation
        // assuming MTMR is always used
        master_topic = "/dvrk/MTML/set_wrench_body_orientation_absolute";
        pub_wrench_abs[1] = node->advertise<std_msgs::Bool>(master_topic.c_str(), 1);
        pub_wrench_abs[1].publish(wrench_body_orientation_absolute);
        ROS_INFO("Setting wrench_body_orientation_absolute on %s", master_topic.c_str());
    }

    ros::Rate loop_rate(haptic_loop_rate);
    ROS_INFO("The desired pose will be updated at '%f'",
             haptic_loop_rate);

    //---------------------------------------------
    // loop

    while (ros::ok())
    {
        VTKConversions::KDLFrameToVTKMatrix(*tool_current_pose_kdl[0],
                                            tool_current_pose[0]);
        // find the center of the ring
        ring_center[0] = *tool_current_pose_kdl[0] *
            KDL::Vector(0.0, 0.0,ring_radius);

        if(bimanual){
            VTKConversions::KDLFrameToVTKMatrix(*tool_current_pose_kdl[1],
                                                tool_current_pose[1]);
            ring_center[1] = *tool_current_pose_kdl[1] *
                KDL::Vector(0.0, ring_radius, 0.0);
        }


        CalculatedDesiredToolPose();

        // publish desired poses
        for (int n_arm = 0; n_arm < 1+ int(bimanual); ++n_arm) {

            // convert to pose message
            geometry_msgs::PoseStamped pose_msg;
            KDL::Frame tool_desired_pose_in_slave_frame;
            tool_desired_pose_in_slave_frame =
                slave_frame_to_world_frame_tr->Inverse()*
            tool_desired_pose_kdl[n_arm];

            tf::poseKDLToMsg(tool_desired_pose_in_slave_frame, pose_msg.pose);
            // fill the header
            pose_msg.header.frame_id = "/task_space";
            pose_msg.header.stamp = ros::Time::now();
            // publish
            pub_desired[n_arm].publish(pose_msg);
        }

        ros::spinOnce();
        loop_rate.sleep();
        boost::this_thread::interruption_point();
    }
}

void TaskBuzzWire::CalculateAndSaveError() {

    double duration = (ros::Time::now() - start_time).toSec();
    double posit_error_avg = posit_error_sum/(double)sample_count;
    double orient_error_avg = orient_error_sum/(double)sample_count;

    double posit_error_avg_ideal = 0.0005;
    double orient_error_avg_ideal = 0.35;
    double posit_error_max_ideal = 0.0015;
    double duration_ideal = 9.0;

    // put a threshold on the values
    if (posit_error_avg < posit_error_avg_ideal)
        posit_error_avg = posit_error_avg_ideal;

    if (orient_error_avg < orient_error_avg_ideal)
        orient_error_avg = orient_error_avg_ideal;

    if (posit_error_max < posit_error_max_ideal)
        posit_error_max = posit_error_max_ideal;

    if (duration < duration_ideal)
        duration = duration_ideal;

    double score = ( posit_error_avg_ideal/posit_error_avg
        + posit_error_max_ideal/posit_error_max
        + duration_ideal/duration
        + orient_error_avg_ideal/orient_error_avg)
        * 100 / 4;

    // when the history gets full we start a new set
    if (score_history.size() == n_score_history)
        ResetScoreHistory();

    score_history.push_back(score);
    score_history_colors.push_back(GetScoreColor(score));

    // update spheres' color
    for (int i = 0; i < score_history.size(); ++i) {
        score_sphere_actors[i]->GetProperty()->SetColor(score_history_colors[i]);

    }

    ROS_INFO("posit_error_max: %f", posit_error_max);
    ROS_INFO("duration: %f", duration);
    ROS_INFO("posit_error_avg: %f", posit_error_avg);
    ROS_INFO("orient_error_avg: %f", orient_error_avg);
    ROS_INFO("Score: %f", score);
    ROS_INFO("  ");
}

double * TaskBuzzWire::GetScoreColor(const double score) {

    //decide the color
    if(score > 90){
        return colors.Green;
    }
    else if (score > 80)
        return colors.Gold;
    else if(score > 60)
        return colors.Orange;
    else
        return colors.Red;

}

void TaskBuzzWire::ResetOnGoingEvaluation() {
    posit_error_sum = 0.0;
    posit_error_max = 0.0;
    orient_error_sum = 0.0;
    sample_count = 0;
}

void TaskBuzzWire::ResetScoreHistory() {
    score_history.clear();
    score_history_colors.clear();

    // reset colors to gray
    for (int i = 0; i < n_score_history; ++i) {
        score_sphere_actors[i]->GetProperty()->SetColor(colors.Gray);

    }

}

TaskBuzzWire::~TaskBuzzWire() {
    delete [] slave_names;
}
