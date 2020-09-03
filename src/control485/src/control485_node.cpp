//
// Created by sjtu_wanghaili on 2020/6/4.
//

#include<iostream>
#include<string>
#include<unistd.h>  /* UNIX standard function definitions */
#include<fcntl.h>   /* File control definitions */
#include<termios.h> /* POSIX terminal control definitions */
#include<sys/time.h>    //for time
#include<modbus.h>
#include<iostream>
#include<fstream>    // 读写文件的头文件
#include<string>
#include<vector>
#include<algorithm>
#include<signal.h>
#include<pthread.h>

#include <ros/ros.h>
#include <std_msgs/Bool.h>
#include "std_msgs/String.h"
#include "std_msgs/Int32.h"
#include "std_msgs/Float32.h"
#include <actionlib/server/simple_action_server.h>
#include "control485/DriveMotorAction.h"


using namespace std;

// 初始化变量
struct harvesterSpeed
{
    double linear=0.0;
    double rotate=0.0;
};
modbus_t* com;//com用于电机速度控制反馈
uint16_t motorModbusAddr=0xB6; //0xB6在说明书中用于使能电机的rs485功能
uint16_t motorDirectionAddr=0x66; //在说明书中找到在0x66中访问数据0x01是正转，0x02是反转
uint16_t motorSpeedAddr=0x56; //在说明书中找到，0x56中设置电机的转速
uint16_t motorSpeedFeedbackAddr=0x5F; //说明书中可以找到其为读取速度的地址
uint16_t motorCurrentFeedbackAddr=0xC6; //说明书中找到而补充的电流读取，但是应该暂时不用（因为不精确吧）
uint16_t motorIDAddr=0x43; //这是在网上找到的，设置从站地址
// 以上，就是现在用到的寄存器地址


double cbCof=1.2,reelCof=1.6,pfCof=4.44,fhCof=3.94;//同调率
int cbRatio=5,reelRatio=64,pfRatio=15,fhRatio=10;//减速比
const int reelMotor=1,cbMotor=2,pfMotor=3,fhMotor=4;
string port="/dev/ttyUSB0";
harvesterSpeed carSpeed;
bool is_obstacle = false;
bool is_stop = false;
ros::Publisher* pub_modified_car_speed;
ros::Publisher* pub_reel_speed;
ros::Publisher* pub_cb_speed;
ros::Publisher* pub_pf_speed;
ros::Publisher* pub_fh_speed;

std_msgs::Float32 modified_car_speed;
float last_modified_car_speed = 0;

string current_time = "";
ofstream* open_file;


// 函数申明
bool openSerial(const char* port);
void* getTime(void*);//获取当前系统时间
void motorInit(void);
void motorSetModbus(int motor,int enable);
void motorSetDirection(int motor,int dir);
void motorSetSpeed(int motor,int speed);
int motorReadSpeed(int motor);
vector<double> motorReadCurrent(void);
pair<double,double> carReadSpeed(void);
void* carSpeedFollowMode(void*);
int getCBSpeed(double carSpeed);
int getReelSpeed(double carSpeed);
int getFHSpeed(double carSpeed);
int getPFSpeed(double carSpeed);
void currentTest(void);
void test(void);
void heightRecord(void);
void carSpeedTest(void);
void singleMotorCurrentRecord(int motor);
void test1(void);
void rotate(void);
void rotateR(void);

bool endFlag = false;


// 测试action
typedef actionlib::SimpleActionServer<control485::DriveMotorAction> Server;

void execute(const control485::DriveMotorGoalConstPtr &goal, Server *as) {
    ROS_INFO("Start Motor %d.", goal->motor_id);

    int target_speed = 0;
    int actual_speed = -1000;
    int actual_speed_pre = -1000;

    // 计算目标速度，读取真实速度
    target_speed = goal->target_speed;
    actual_speed = motorReadSpeed(goal->motor_id);

    ROS_WARN_STREAM("the current speed is: "<<actual_speed);
    ROS_WARN_STREAM("the target speed is: "<<target_speed);
    ROS_WARN_STREAM("the difference of speed still: "<<abs(target_speed - actual_speed));

    motorSetSpeed(goal->motor_id, target_speed);

    int count = 0;
    while (true) {
        usleep(20000);
        actual_speed = motorReadSpeed(goal->motor_id);

        // 记录速度
        string actual_speed_str = to_string(actual_speed);
        while (actual_speed_str.size()<4)
        {
            actual_speed_str="0"+actual_speed_str;
        }
        *(open_file) << current_time << " " << goal->motor_id << " " <<actual_speed_str << " " << carSpeed.linear << endl;


        if (abs(actual_speed - actual_speed_pre) < 50)
        {
            ROS_WARN_STREAM("Steady count ++");
            count++;
        }
//        if (abs(actual_speed - target_speed) < 50)
//        {
//            ROS_WARN_STREAM("Steady count ++");
//            count++;
//        }
        else
        {
            usleep(20000);
            motorSetSpeed(goal->motor_id, target_speed);
        }
        
        if(count > 3)
        {
            ROS_INFO_STREAM("speed was steaby.");
            switch (goal->motor_id) {
                case 2:
                {
                    ROS_INFO_STREAM("pub reel speed.");
                    std_msgs::Float32 reel_speed;
                    reel_speed.data = actual_speed;
                    pub_reel_speed->publish(reel_speed);
                    ROS_INFO_STREAM("pubed reel speed.");
                    break;
                }
                case 1:
                {
                    ROS_INFO_STREAM("pub cb speed.");
                    std_msgs::Float32 cb_speed;
                    cb_speed.data = actual_speed;
                    pub_cb_speed->publish(cb_speed);
                    break;
                }
                case 3:
                {
                    ROS_INFO_STREAM("pub pf speed.");
                    std_msgs::Float32 pf_speed;
                    pf_speed.data = actual_speed;
                    pub_pf_speed->publish(pf_speed);
                    break;
                }
                case 4:
                {
                    ROS_INFO_STREAM("pub fh speed.");
                    std_msgs::Float32 fh_speed;
                    fh_speed.data = actual_speed;
                    pub_fh_speed->publish(fh_speed);
                    break;
                }

            }
            break;
        }

        ROS_WARN_STREAM("pre and actual:" <<actual_speed_pre<<" "<< actual_speed);
        actual_speed_pre = actual_speed;
    }

    cout << "carVl=" << carSpeed.linear << " carVw=" << carSpeed.rotate <<
         " realv=" << actual_speed << " realvNew=" << target_speed << endl;

    as->setSucceeded();

    ROS_INFO_STREAM("Action complete. Wait for next invoke.\n");
}


//在程序退出前 调用电机停止指令
static void my_handler(int sig){ // can be called asynchronously
    endFlag = 1; // set flag
}

bool openSerial(const char* port)
{
    com=modbus_new_rtu(port,9600,'N',8,1);
    if(com==nullptr)
    {
        cout<<"wrong modbus parameter."<<endl;
        return false;
    }
    timeval time_out;
    time_out.tv_sec=0;
    time_out.tv_usec=1000*100;
    modbus_set_response_timeout(com,time_out.tv_sec,time_out.tv_usec);
    modbus_rtu_set_serial_mode(com,MODBUS_RTU_RS485);
    if(modbus_connect(com)==-1)
    {
        cout<<"Cannot connect modbus at port:"<<port<<endl;
        return false;
    } else
        cout<<"Connected modbus at port:"<<port<<endl;
    return true;
}
void* getTime(void*)
{
    // 时间在后台20ms更新一次
    while(!endFlag)
    {
        timeval tv;
        time_t timep;
        tm* timeNow;
        gettimeofday(&tv, NULL);//获取当下精确的s和us的时间
        time(&timep);//获取从1900年至今的秒数
        timeNow = gmtime(&timep); //注意tm_year和tm_mon的转换后才是实际的时间
        timeNow->tm_year+=1900;//实际年
        timeNow->tm_mon+=1;//实际月
        timeNow->tm_hour+=8;//实际小时
        if(timeNow->tm_hour>=24)
        {
            timeNow->tm_hour-=24;
        }
        long int ms = (tv.tv_sec*1000.0 + tv.tv_usec / 1000.0) - timep * 1000.0; //实际ms

        current_time ="";
        current_time+='[';
        current_time+=to_string(timeNow->tm_year);
        current_time+='-';
        current_time+=to_string(timeNow->tm_mon);
        current_time+='-';
        current_time+=to_string(timeNow->tm_mday);
        current_time+=' ';
        current_time+=to_string(timeNow->tm_hour);
        current_time+=':';
        current_time+=to_string(timeNow->tm_min);
        current_time+=':';
        string s_string = to_string(timeNow->tm_sec);
        while (s_string.size()<2)
        {
            s_string="0"+s_string;
        }
        current_time+=s_string;
        current_time+=':';
        string ms_string = to_string(int(ms));
        while (ms_string.size()<3)
        {
            ms_string="0"+ms_string;
        }
        current_time+=ms_string;
        current_time+=']';

//        ROS_WARN_STREAM("current time: "<<current_time);
        // todo 获取时间的函数好像执行很慢，感觉可以之后开一个线程让它在后台慢慢执行
        usleep(20000);
    }
    ROS_WARN_STREAM("current time sync stoped.");
//    endFlag = false;
}
void motorInit(void)
{
    // 只有这里才打开了电机，这里首先仅仅开启了reel电机
    ROS_WARN_STREAM("init reelmotor...");
    motorSetModbus(reelMotor,1);
    motorSetDirection(reelMotor,1);//正转
//    motorSetSpeed(reelMotor,0);

    ROS_WARN_STREAM("init cbmotor...");
    motorSetModbus(cbMotor,1);
    motorSetDirection(cbMotor,1);//正转
//    motorSetSpeed(cbMotor,0);

    ROS_WARN_STREAM("init pfmotor...");
    motorSetModbus(pfMotor,1);
    motorSetDirection(pfMotor,2);//正转
//    motorSetSpeed(pfMotor,0);

    ROS_WARN_STREAM("init fhmotor...");
    motorSetModbus(fhMotor,1);
    motorSetDirection(fhMotor,1);//正转
//    motorSetSpeed(fhMotor,0);
}

// 使能某电机的rs485通讯
void motorSetModbus(int motor,int enable)
{
    modbus_set_slave(com,motor); //这句话的意思是不是将某从机设置为当前要访问的对象？
    usleep(20000);
    while (!modbus_write_register(com,motorModbusAddr,enable))
    {
        ROS_WARN_STREAM(motor<<"set modbus failed.");
        usleep(20000);
    }
}
void motorSetDirection(int motor,int dir)
{
    modbus_set_slave(com,motor);
    usleep(20000);
    while (!modbus_write_register(com,motorDirectionAddr,dir))
    {
        ROS_WARN_STREAM(motor<<"set direction failed, try again.");
        usleep(20000);
    }
}
void motorSetSpeed(int motor,int speed)
{
    if(speed>3000)
    {
        speed=3000;
    }
    if(speed<0)
    {
        speed=0;
    }

    modbus_set_slave(com,motor);
//    ROS_WARN_STREAM("set speed result: "<<modbus_write_register(com,motorSpeedAddr,speed));
    usleep(20000);
    while (!modbus_write_register(com,motorSpeedAddr,speed))
    {
        ROS_WARN_STREAM(motor<<"set speed failed， try again.");
        usleep(20000);
    }
}
int motorReadSpeed(int motor)
{
    ROS_INFO_STREAM("Will control motor: "<<motor);
    uint16_t temp=-1000;
    modbus_set_slave(com,motor);
    int faild_num = 0;
    int flag = -1;
    do {
        usleep(20000);
        flag = modbus_read_registers(com, motorSpeedFeedbackAddr, 1, &temp);
        if (flag == -1) {
            cout << "error read motor" << motor << " speed." << endl;
            faild_num++;
        } else {
            cout << "succeed read motor" << motor << " speed." << endl;
        }
    } while (flag == -1 && faild_num<10);
    return temp;
}

// —————— 该函数已废弃 ————————  //
pair<double,double> carReadSpeed(void)
{
    uint16_t temp[14];
    //modbus_set_debug(com,true);
    modbus_set_slave(com,5);//车速信号 can-modbus 从站序号为5
    if(modbus_read_registers(com,0x00,0X0E,temp)==-1)
    {
        cout<<"error reading carspeed,slave=5"<<endl;
    }
//    usleep(3000);
    double lSpeed=0.001,rSpeed=0.001;
    if(temp[7]==0&&temp[7]==0&&temp[8]==0&&temp[9]==0)//车未启动
    {
        return {0.0,0.0};
    }
    temp[7]=(temp[7])<<8;//直线车速高8位 左移8位
    temp[7]=(temp[7])|(temp[6]);//直线车速高8位|直线车速低8位
    int l=temp[7];
    l-=32768;
    temp[9]=(temp[9])<<8;//旋转车速高8位 左移8位
    temp[9]=(temp[9])|(temp[8]);//旋转车速高8位|旋转车速低8位
    int w=temp[9];
    w-=32768;
    lSpeed*=l;
    rSpeed*=w;
    if(lSpeed>5)
    {
        lSpeed=0;
    }
    else if(lSpeed<-5)
    {
        lSpeed=0;
    }
    if(rSpeed>5)
    {
        rSpeed=0;
    }
    else if(rSpeed<-5)
    {
        rSpeed=0;
    }
    return {lSpeed,rSpeed};
}
// —————— 该函数已废弃 ———————— //

int getReelSpeed(double carSpeed)
{
    if(carSpeed<0)
    {
        carSpeed=0;
    }
    int res= reelRatio*min(50.0,min(21.23*reelCof*carSpeed+12.3,21.23*1.0*carSpeed+21.23));
    if(is_obstacle == true || is_stop == true)
        res = 0;
    return min(res,3000);
}
int getCBSpeed(double carSpeed)
{
    if(carSpeed<0)
    {
        carSpeed=0;
    }
    int res= cbRatio*min(467.0,min(398.09*cbCof*carSpeed+131.37,398.09*1.0*carSpeed+238.85));
    if(is_obstacle == true || is_stop == true)
        res = 0;
    return min(res,3000);
}
int getPFSpeed(double carSpeed)
{
    if(carSpeed<0)
    {
        carSpeed=0;
    }
    int res= pfRatio*min(187.0,min(39.16*pfCof*carSpeed+52.47,39.16*3.0*carSpeed+90.07));
    if(is_obstacle == true || is_stop == true)
        res = 0;
    return min(res,3000);
}
int getFHSpeed(double carSpeed)
{
    if(carSpeed<0)
    {
        carSpeed=0;
    }
    int res= fhRatio*min(324.0,min(76.43*fhCof*carSpeed+90.95,76.43*3.0*carSpeed+152.86));
    if(is_obstacle == true || is_stop == true)
        res = 0;
    return min(res,3000);
}

void* carSpeedFollowMode(void*)
{
    while(!endFlag)
    {
        if(is_stop || is_obstacle){
            modified_car_speed.data = -1;
        } else{
            modified_car_speed.data = carSpeed.linear;
        }

        if(modified_car_speed.data != last_modified_car_speed){
            ROS_WARN_STREAM("modified speed will be change.");
        }
        pub_modified_car_speed->publish(modified_car_speed);
        last_modified_car_speed = modified_car_speed.data;
    }
    ROS_WARN_STREAM("carspeedfollow stoped.");
//    endFlag = false;
}

// --- 函数暂时废弃 --- //
vector<double> motorReadCurrent(void)
{
    vector<double> current(4);
    uint16_t analog[4]={0,0,0,0};
    modbus_set_slave(com,6);
    //slave=6 address reel=0x02 cb=0x03 pf=0x04 fh=0x05
    int flag=modbus_read_registers(com,2,4,analog);
    if(flag==-1)
    {
        cout<<"error read current."<<endl;
    }
//    usleep(3000);
    for(int i=0;i<4;i++)
    {
        //voltage=analog/32767*1.2
        //current=vlotage/rangeV*rangeI (0-70A: 0-5V)
        current[i]=analog[i]/32767.0*1.2*5*14;
    }
    //reeli=current0 cbi=current[1] pfi=current[2] fhi=current[3]
    return current;
}
double readHeight(void)
{
    double height;
    uint16_t analog;
    modbus_set_slave(com,6);
    //slave=6 height=0x08
    int flag=modbus_read_registers(com,8,1,&analog);
    if(flag==-1)
    {
        cout<<"error read grain heap height."<<endl;
    }
//    usleep(2000);
    //voltage=analog/32767.0*1.2*5
    //height=0.2+voltage/5*(3-0.2)
    height=0.2+analog/32767.0*1.2*5.0/5.0*(3-0.2);
    return height;
}
// --- --- //

void car_speed_callback(const std_msgs::Float32ConstPtr &msg);
void is_obstacle_callback(const std_msgs::BoolConstPtr &msg);
void is_stop_callback(const std_msgs::BoolConstPtr &msg);

int main (int argc, char **argv)
{
    ros::init(argc, argv, "hello") ;
    ROS_INFO_STREAM("Hello, ROS!") ;
    ros::NodeHandle n_;

    pthread_t time_sync;
    pthread_create(&time_sync, nullptr, getTime, nullptr);
    ROS_INFO_STREAM("time sync spread make.");

    ofstream ofs;
    string filename = "/home/sjtu_wanghaili/yammar_ws/speed_result/";
    filename = filename + current_time + "_speed.txt";
    ofs.open(filename, ios::out);
    if(!ofs)
    {
        cerr<<"Open File Fail."<<endl;
        exit(1);
    }
    open_file = &ofs;

    ros::Subscriber sub2_;
    ros::Subscriber sub3_;
    ros::Subscriber sub_;

    ros::Publisher pub_;
    ros::Publisher pub1_;
    ros::Publisher pub2_;
    ros::Publisher pub3_;

    pub_ = n_.advertise<std_msgs::Float32>("modified_car_speed", 1);
    pub_modified_car_speed = &pub_;

    pub1_ = n_.advertise<std_msgs::Float32>("REEL_speed", 1);
    pub_reel_speed = &pub1_;

    pub2_ = n_.advertise<std_msgs::Float32>("CB_speed", 1);
    pub_cb_speed = &pub2_;

    pub3_ = n_.advertise<std_msgs::Float32>("PF_speed", 1);
    pub_pf_speed = &pub3_;

    //Topic you want to subscribe
    sub2_ = n_.subscribe("is_obstacle", 1, &is_obstacle_callback);
    sub3_ = n_.subscribe("is_stop", 1, &is_stop_callback);
    sub_ = n_.subscribe("car_speed", 1, &car_speed_callback);

    cout<<"usage sudo ./motor"<<endl;
    //modbus_set_debug(com,true);//调试模式 可以显示串口总线的调试信息
    openSerial(port.c_str());
    motorInit();

    pthread_t motorControlThread;
    pthread_create(&motorControlThread, nullptr, carSpeedFollowMode, nullptr);
    ROS_INFO_STREAM("motor control spread make.");

    // 定义一个服务器，control485就是topic
    Server server(n_, "control485", boost::bind(&execute, _1, &server), false);
    // 服务器开始运行
    server.start();
    ros::spin();

    ROS_INFO_STREAM("wait spread close.");
    endFlag = true;
    pthread_kill(motorControlThread, 0);
    pthread_kill(time_sync, 0);
    ros::Duration(10);

    // todo 停下所有电机
    motorSetSpeed(1, 0);
    usleep(100000);
    motorSetSpeed(2, 0);
    usleep(100000);
    motorSetSpeed(3, 0);
    usleep(100000);
    ofs.close();
    return 0;
}

void is_obstacle_callback(const std_msgs::BoolConstPtr &msg) {
//    ROS_INFO_STREAM("callback! is_obstacle: "<<msg->data);
    is_obstacle = msg->data;
}

void is_stop_callback(const std_msgs::BoolConstPtr &msg) {
    ROS_INFO_STREAM("callback! is_stop: "<<msg->data);
    is_stop = msg->data;
}

void car_speed_callback(const std_msgs::Float32ConstPtr &msg) {
    ROS_INFO_STREAM("callback! carspeed: "<<msg->data);
    carSpeed.linear = msg->data;
    carSpeed.rotate = 0;
}
