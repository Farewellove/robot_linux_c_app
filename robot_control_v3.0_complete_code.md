# 爬壁机器人焊接检测控制系统 v3.0 — 完整程序代码

> **项目名称**: robot_control_v3.0（爬壁机器人控制上位机）
> **开发框架**: Qt 5 (C++11)
> **用途场景**: 中国研究生电子设计竞赛（研电赛）
> **核心依赖**: Qt Widgets, Qt Network (TCP), Qt SerialPort, OpenGL, OpenCV 4

---

## 文件列表

| 序号 | 文件 | 说明 |
|------|------|------|
| 1 | `robot_control.pro` | Qt 工程文件 |
| 2 | `config.h` | 全局常量、枚举、结构体定义 |
| 3 | `main.cpp` | 程序入口 |
| 4 | `mainwindow.h` | 主窗口头文件 |
| 5 | `mainwindow.cpp` | 主窗口实现（TCP通信/摄像头/数据解析/UI交互） |
| 6 | `mainwindow.ui` | 主窗口 UI 布局文件 |
| 7 | `openglwidget.h` | OpenGL 3D 渲染组件头文件 |
| 8 | `openglwidget.cpp` | OpenGL 3D 渲染实现（圆柱罐/球罐建模、焊缝绘制） |
| 9 | `openglwidget.ui` | OpenGL Widget UI 文件 |

---

## 1. robot_control.pro（Qt 工程文件）

```qmake
QT       += core gui network opengl
QT       += serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
#CONFIG += console # 打开命令行窗口

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    openglwidget.cpp

HEADERS += \
    config.h \
    mainwindow.h \
    openglwidget.h

FORMS += \
    mainwindow.ui \
    openglwidget.ui

INCLUDEPATH += /usr/local/include/opencv4 \
               /usr/local/include/opencv4/opencv2

LIBS += /usr/local/lib/libopencv_highgui.so \
        /usr/local/lib/libopencv_core.so    \
        /usr/local/lib/libopencv_imgproc.so \
        /usr/local/lib/libopencv_imgcodecs.so \
        /usr/local/lib/libopencv_videoio.so

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32:LIBS += -lOpengl32 -lglu32
unix:LIBS +=  -lGLU #-lglut
```

---

## 2. config.h（全局常量、枚举、结构体定义）

```cpp
#ifndef CONFIG_H
#define CONFIG_H

#define car2pc 19  //总位数20-1
#define PI 3.1415926
#include <QTcpSocket>

#define CYLINDERPTNUM 3000
#define NUMPERDEG  4
#define CYLINDERCROSSPTNUM 8
#define CYLINDERROUTNUM 12

///球罐相关
#define SPHERECROSSPTNUM 104
#define SPHEREPTNUM    (3600*NUMPERDEG)
#define SPHEREROUTNUM  300

typedef enum state{
    FINDING_0X55   = 0x00,
    //NEED_TAG   = 0x01,
    GETTING_DATA =0x02
} RX_STATE;

typedef enum tcp_state{
    FINDING_TCP_0X66   = 0x00,
    //NEED_TAG   = 0x01,
    GETTING_TCP_DATA =0x02
} TCP_RX_STATE;

struct Rx_CarData {
    uint8_t data[car2pc];
    uint8_t num;
};

struct SpherePT      //节点
{
    float x,y,z;           //笛卡尔坐标x,y,z
    float angle1,angle2;   //经度，纬度
};
struct SphereWeldseam
{
    int visitednum;  //被访问的次数
    int dig; //0表示经线 1 表示纬线 2表示其他，需要另外计算得到
};
struct SphereCrossPT    //交叉点
{
    int ptnum;
    int endptnum[3];  //以该交叉点为起点的焊缝的终止节点编号
    float angle1,angle2;
};
struct SphereRoute       //提前规划好的路径
{
    int ptnum;         //当前交叉点
    int nextptnum;     //下一个交叉点
    int visitedtime;  //两个交叉点构成的边被访问的次数，0,1, -影响机器人运动的速度
    int dig;           //倾斜角  0 表示沿经线  1 表示沿纬线  其他  ：51 52 61 62 71 72  表示上极 椭圆5 椭圆6 椭圆7  53 54 63 64 73 74
};
struct SphereRouteNum    //对于非经线 纬线的焊缝路径 给定 轨迹在 sphPt【】中的位置
{
    int from;
    int to ;
};
#endif // CONFIG_H
```

---

## 3. main.cpp（程序入口）

```cpp
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
```

---

## 4. mainwindow.h（主窗口头文件）

```cpp
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QDebug>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include "config.h"
#include <QHostInfo>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>

#include <opencv2/opencv.hpp>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/video.hpp>


#include<QMessageBox>
#include<QFileDialog>
#include<QImage>
#include<QImageReader>
#include <QBuffer>


//opengl
#include<QOpenGLWindow>
#include<QOpenGLFunctions>



QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

using namespace cv;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    //QSerialPort *serial;
    QTimer *Timer_tcp,*Timer_camera_tcp,*Timer_data_save;
    QTcpSocket *client_socket,*camera_client_socket,*camera_serversocket;
    QTcpServer *server,*camera_server;

    int test=1;
    int control_mod=0;
    int car_enable=0;
    uint8_t control_vx=0;
    uint8_t control_vy=0;
    uint8_t control_vz=0;
    double currrnt_vx=0;
    double currrnt_vy=0;
    double currrnt_vz=0;

    VideoCapture capture;
    Mat frame;
    Mat img_video;

    qint64 camera_imageBlockSize;
    QImage img_save;
    QFile data_file;
    VideoWriter video_save;
    bool img_video_state=false;


private slots:

    void Timer_tcp_Update();
    void client_socket_Read_Data();
    void client_socket_Disconnected();

    void camera_client_Read_Data();
    void camera_client_Disconnected();
    void camera_cilent_showimage(QByteArray tcp_img);

    void Timer_data_save_Update();
    void dealUwbData(uint8_t*);
    void dealCarData(int8_t*);
    QHostAddress getIP();

    void on_radioButton_clicked();
    void on_radioButton_2_clicked();
    void on_radioButton_3_clicked();
    void on_control_vx_text_editingFinished();
    void on_control_vy_text_editingFinished();
    void on_control_vz_text_editingFinished();
    void on_pushButton_2_clicked();
    void on_pushButton_clicked();
    void on_tcp_connect_button_clicked();

    void on_camera_client_connectButton_clicked();
    void on_tcp_camera_requestButton_clicked();

    void camera_server_Newconnection();
    void camera_server_readRequest();
    void on_camera_server_listen_clicked();
    QByteArray recvall(QTcpSocket *,int count);

    void on_img_save_button_clicked();
    void on_save_data_clicked();
    void on_video_save_button_clicked();


    void on_reset_clicked();
    void on_comboBox_activated(int index);

};
#endif // MAINWINDOW_H
```

---

## 5. mainwindow.cpp（主窗口实现）

```cpp
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QTime>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //获取本机ip
    QHostAddress ipAddr=getIP();

    //TCP 设置
    client_socket = new QTcpSocket();
    QObject::connect(client_socket, &QTcpSocket::readyRead, this, &MainWindow::client_socket_Read_Data);
    QObject::connect(client_socket, &QTcpSocket::disconnected, this, &MainWindow::client_socket_Disconnected);

    ui->Server_IP->setText("192.168.1.103");
    ui->lineEdit_Port->setText("9090");

    //camera_client
    camera_client_socket = new QTcpSocket();
    QObject::connect(camera_client_socket, &QTcpSocket::readyRead, this, &MainWindow::camera_client_Read_Data);
    QObject::connect(camera_client_socket, &QTcpSocket::disconnected, this, &MainWindow::camera_client_Disconnected);
    //connect(camera_client_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(error()));//套接字的错误消息信号
    camera_imageBlockSize = 0;		//一次接收的图像数据的大小（字节数）
    ui->tcp_camera_requestButton->setEnabled(false);

    camera_server = new QTcpServer();	//服务器端监听套接字 ServerListenSocket
    connect(camera_server, SIGNAL(newConnection()), this, SLOT(camera_server_Newconnection()));	//将监听套接字有新客户端的接入信号newConnection() 的与处理的槽函数clientConnection() 连接


     //定时器 Timer_tcp
     Timer_tcp = new QTimer();
     connect(Timer_tcp,SIGNAL(timeout()),this,SLOT(Timer_tcp_Update()));

}

void MainWindow::camera_server_Newconnection()  //有新客户端连接的处理函数
{
    qDebug() <<"An new client is connected!";

    camera_serversocket = camera_server->nextPendingConnection();	//返回已连接套接字对象
    connect(camera_serversocket, SIGNAL(readyRead()), this, SLOT(camera_server_readRequest()));	//将已连接套接字对象的准备好可读信号readyRead与 readClientRequest()槽函数连接
    //connect(camera_serversocket, SIGNAL(disconnected()), camera_serversocket,SLOT(deleterLater()));	//已连接套接字的断开信号与自身的稍后删除信号相连接

}

void MainWindow::camera_server_readRequest()
{
//    static QByteArray Buffer11;
//    Buffer11 = camera_serversocket->readAll();
//    //qDebug(Buffer);
    QByteArray buf_long ;
    QByteArray buf_image ;
    static int long_read=16;
    if (long_read==16)
    {
        buf_long=recvall(camera_serversocket,long_read);
        long_read=buf_long.toInt();
        //qDebug(buf_long);
    }
    else
    {
        buf_image=recvall(camera_serversocket,long_read);
        long_read=16;
        //qDebug(buf_image);

        QImage img;
        //img.loadFromData(rdc,"JPEG");//解释为jpg格式的图像
        img.loadFromData(buf_image);//解释为jpg格式的图像

        ui->camera_img_show->setPixmap(QPixmap::fromImage(img));

    }



    //int buf_x;
    //memcpy(&buf_x, buf_long.data(), 16);
    //buf_image=recvall(buf_x);


}

QByteArray MainWindow::recvall(QTcpSocket * socket,int count)
{
    QByteArray buf ="";
    while (count)
    {   static QByteArray Buffer;
        Buffer = socket->read(count);
        if(Buffer=="")
        {
          return 0;
        }
        buf += Buffer;
        count -= Buffer.length();
    }
    return buf;
}

MainWindow::~MainWindow()
{
    delete ui;
    server->close();
    server->deleteLater();
}

QHostAddress MainWindow::getIP()
{
    QList<QHostAddress> AddressList = QNetworkInterface::allAddresses();
    QHostAddress result;
     foreach(QHostAddress address, AddressList){
         if(address.protocol() == QAbstractSocket::IPv4Protocol &&
            address != QHostAddress::Null &&
            address != QHostAddress::LocalHost){
             if (address.toString().contains("127.0.")){
               continue;
             }
             result = address;
             //qDebug()<<"本机ip"<<address.toString();

             QString  str;
             str += tr("本机ip:")+ address.toString();
             ui->Command_text->append(str);

             ui->host_IP->setText(address.toString());
             break;
         }
     }

     return result;;
}

void MainWindow::on_tcp_connect_button_clicked()
{
    if(ui->tcp_connect_button->text() == tr("连接机器人"))
    {
        QString IP;
        int port;

        //获取IP地址
        IP = ui->Server_IP->text();
        //获取端口号
        port = ui->lineEdit_Port->text().toInt();

        //取消已有的连接
        client_socket->abort();
        //连接服务器
        client_socket->connectToHost(IP, port);

        //等待连接成功
        if(!client_socket->waitForConnected(100))
        {
            qDebug() << "Connection failed!";

            QString  str;
            str += tr("Connection failed!");
            ui->Command_text->append(str);

            return;
        }
        qDebug() << "Climbiongrobot Connect successfully!";

        QString  str;
        str += tr("Climbiongrobot Connect successfully");
        ui->Command_text->append(str);

        Timer_tcp->start(10);

        //发送按键使能
        ui->tcp_connect_button->setEnabled(true);
        //修改按键文字
        ui->tcp_connect_button->setText("断开连接");
        ui->tcp_connect_button->setStyleSheet("background-color: rgb(255,0,0);");
        //ui->server_listen->setEnabled(false);
        //ui->server_send_Button->setEnabled(false);


    }
    else
    {
        //断开连接
        client_socket->disconnectFromHost();
        //修改按键文字
        ui->tcp_connect_button->setText("连接机器人");
        ui->tcp_connect_button->setStyleSheet("background-color: rgb(0,255,0);");
        ui->tcp_connect_button->setEnabled(true);
        Timer_tcp->stop();
    }
}

void MainWindow::Timer_tcp_Update()
{

    // QObject::connect(serial,&QSerialPort::readyRead,this,&MainWindow::WriteUnChar);
     // serial->write(ui->X_Speed_TEXT->toPlainText().toLatin1());
    static int send_i;

     unsigned char client_send_buf[20];
     client_send_buf[0] = 0X77;
     client_send_buf[1] = 0X00;

     client_send_buf[2] = car_enable;
     client_send_buf[3] = control_mod;
     client_send_buf[4] = 0X00;

     memcpy(client_send_buf+5,&control_vx,1);
     memcpy(client_send_buf+6,&control_vy,1);
     memcpy(client_send_buf+7,&control_vz,1);
//     client_send_buf[5] = control_vx+128;
//     client_send_buf[6] = control_vy+128;
//     client_send_buf[7] = control_vz+128;

     for(int i=8;i<17;i++)
     {
      client_send_buf[i] =0;
     }

     client_send_buf[17] = 0X00;
     client_send_buf[18] = send_i;
     client_send_buf[19] = 0X0A;
     send_i++;

     client_socket->write(reinterpret_cast<const char*>(&client_send_buf),20);
     //socket->flush();

      test++;
      //WriteUnChar(WRITE_BUF, 19);

       /*QString str ;
       ui->tcp_write_text->clear();
       str=tr("发送数据: ");
      for (int i=0;i<20;i++)
      {
          QString str = ui->tcp_write_text->toPlainText();
          str += QString("%1").arg(client_send_buf[i]&0xFF,2,16,QLatin1Char('0')).toUpper() + QString(tr(" "));
          ui->tcp_write_text->clear();
          ui->tcp_write_text->append(str);
       }*/

}

void MainWindow::dealUwbData(uint8_t* buffer){
    static float x,y,z;
    static int tagNum;
    if((uint8_t)buffer[1] == 'p'){
        memcpy((uint8_t*)&tagNum,(uint8_t*)(buffer+2),4);
        memcpy((uint8_t*)&x,(uint8_t*)(buffer+6),4);
        memcpy((uint8_t*)&y,(uint8_t*)(buffer+10),4);
        memcpy((uint8_t*)&z,(uint8_t*)(buffer+14),4);

        ui->widget->ptofinrob_x.push_back(x);
        ui->widget->ptofinrob_y.push_back(y);
        ui->widget->ptofinrob_z.push_back(z);
        ui->lineEdit_7->setText(QString::number(x,'f',3));
        ui->lineEdit_8->setText(QString::number(y,'f',3));
        ui->lineEdit_9->setText(QString::number(z,'f',3));
    }
}
void MainWindow::dealCarData(int8_t* buffer){
    static short int yaw_i,roll_i,pitch_i,dAngle_i,weld_state;
    static float vx,vy,vz,dAngle,yaw,roll,pitch;
    static float dDis;
    vx = buffer[1]/100.0f;
    vy = buffer[2]/100.0f;
    vz = buffer[3]/100.0f;

    memcpy((uint8_t*)&weld_state,(uint8_t*)(buffer+4),2);
    memcpy((uint8_t*)&dAngle_i,(uint8_t*)(buffer+6),2);
    dAngle=dAngle_i/100.00;
    memcpy((uint8_t*)&dDis,(uint8_t*)(buffer+8),4);

    memcpy((uint8_t*)&yaw_i,(uint8_t*)(buffer+12),2);
    memcpy((uint8_t*)&roll_i,(uint8_t*)(buffer+14),2);
    memcpy((uint8_t*)&pitch_i,(uint8_t*)(buffer+16),2);
    yaw = yaw_i/32768.0*180;
    roll = roll_i/32768.0*180;
    pitch = pitch_i/32768.0*180;

    ui->current_vx_text->setText(QString::number(vx,'f',3));
    ui->current_vy_text->setText(QString::number(vy,'f',3));
    ui->current_vz_text->setText(QString::number(vz,'f',3));
    ui->current_vx_text_3->setText(QString::number(yaw,'f',3));
    ui->current_vy_text_3->setText(QString::number(roll,'f',3));
    ui->current_vz_text_3->setText(QString::number(pitch,'f',3));
    ui->weld_angle_text->setText(QString::number(dAngle,'f',3));
    ui->weld_distance_text->setText(QString::number(dDis,'f',3));
    ui->weld_state_text->setText(QString::number(weld_state));
}
 void MainWindow::client_socket_Read_Data()
 {
     static QByteArray bufferRec;
     static uint8_t* Recdata;
     while(client_socket->bytesAvailable() >= 20){
         bufferRec = recvall(client_socket,20);
         if((uint8_t)bufferRec[18] == 0x55 && (uint8_t)bufferRec[19] == 0x56){
             if((uint8_t)bufferRec[0] == 'u'){
                 Recdata = (uint8_t*)bufferRec.data();
                 dealUwbData(Recdata);
             }
             else if((uint8_t)bufferRec[0] == 'c'){
                 Recdata = (uint8_t*)bufferRec.data();
                 dealCarData((int8_t*)Recdata);
             }
             else
                 bufferRec = client_socket->readAll();
         }
         else
             bufferRec = client_socket->readAll();
     }


//     static QByteArray Buffer;
//     uint8_t* data;
//     int len = 0;
//     Buffer = client_socket->readAll();

//     data = (uint8_t*)Buffer.data();
//     len = Buffer.size();

//     static TCP_RX_STATE RxState = FINDING_TCP_0X66;
//     static Rx_CarData car_data;

//     QString str = ui->tcp_read_text->toPlainText();

//     for(int i = 0;i<len;i++){
//         switch (RxState) {
//             case FINDING_TCP_0X66:
//                 car_data.num = 0;
//                 if(data[i] == 0x66)
//                     RxState = GETTING_TCP_DATA;
//                     str=tr("read:66 ");

//             break;

//             case GETTING_TCP_DATA:
//                     car_data.data[car_data.num] = data[i];

//                     str += QString("%1").arg(car_data.data[car_data.num],2,16,QLatin1Char('0')).toUpper() + QString(tr(" "));
//                     ui->tcp_read_text->clear();
//                     ui->tcp_read_text->append(str);
//                     car_data.num++;

//                     //qDebug("%d,%d",car_data.data[car_data.num],car_data.num);

//                     if(car_data.num == car2pc){
//                         if(car_data.data[car2pc-1] == 0x0A){

//                               QString str_x,str_y,str_z;
//                               str_x=QString::number(double(car_data.data[2]-128)/100.00);
//                               ui->current_vx_text->setText(str_x);
//                               currrnt_vx=double(car_data.data[2]-128)/100.00;

//                               str_y=QString::number(double(car_data.data[3]-128)/100.00);
//                               ui->current_vy_text->setText(str_y);
//                               currrnt_vx=double(car_data.data[3]-128)/100.00;

//                               str_z=QString::number(double(car_data.data[4]-128)/100.00);
//                               ui->current_vz_text->setText(str_z);
//                               currrnt_vx=double(car_data.data[4]-128)/100.00;

//                         }
//                         RxState = FINDING_TCP_0X66;
//                        }
//                  break;
//            }

//     }


 }


void MainWindow::client_socket_Disconnected()
{
    //发送按键失能
    ui->tcp_connect_button->setEnabled(true);
    //修改按键文字
    ui->tcp_connect_button->setText("连接机器人");
    ui->tcp_connect_button->setStyleSheet("background-color: rgb(0,255,0);");

    Timer_tcp->stop();
    qDebug() << "Climbiongrobot Disconnected!";

    QString  str;
    str += tr("Climbiongrobot Disconnected!");
    ui->Command_text->append(str);
}


void MainWindow::on_radioButton_clicked()
{
  control_mod=0;
}

void MainWindow::on_radioButton_2_clicked()
{
   control_mod=1;
}

void MainWindow::on_radioButton_3_clicked()
{
  control_mod=2;
}

void MainWindow::on_control_vx_text_editingFinished()
{
    control_vx =(ui->control_vx_text->text().toFloat())*100.0;;

    qDebug()<<(control_vx);
}

void MainWindow::on_control_vy_text_editingFinished()
{
    control_vy =(ui->control_vy_text->text().toFloat())*100.0;;
}

void MainWindow::on_control_vz_text_editingFinished()
{
    control_vz =(ui->control_vz_text->text().toFloat())*100.0;
}

void MainWindow::on_pushButton_2_clicked()
{
    car_enable=1;
}

void MainWindow::on_pushButton_clicked()
{
    car_enable=0;
}


void MainWindow::on_camera_client_connectButton_clicked()
{
    if(ui->camera_client_connectButton->text() == tr("开启连接"))
    {
        QString IP;
        //获取IP地址
        IP = ui->Server_IP->text();
        //取消已有的连接
        camera_client_socket->abort();
        //连接服务器
        camera_client_socket->connectToHost(IP, 9999);
        //等待连接成功
        if(!camera_client_socket->waitForConnected(100))
        {
            qDebug() << "Robot Camera Connection failed!";
            //ui->command_info->addItem("Robot Camera Connection failed!");
            return;
        }
        qDebug() << "Climbiongrobot Connect successfully!";
       // ui->command_info->addItem("Climbiongrobot Connect successfully!");


        //发送按键使能
        ui->tcp_camera_requestButton->setEnabled(true);
        //修改按键文字
        ui->camera_client_connectButton->setText("断开连接");
        ui->camera_client_connectButton->setStyleSheet("background-color: rgb(255,0,0);");
        //ui->camera_server_listen->setEnabled(false);


    }
    else
    {
        //断开连接
        camera_client_socket->disconnectFromHost();
        //修改按键文字
        ui->camera_client_connectButton->setText("开启连接");
        ui->camera_client_connectButton->setStyleSheet("background-color: rgb(0,255,0);");
        ui->camera_client_connectButton->setEnabled(true);
    }
}

void MainWindow::camera_client_Read_Data()
{
    static int camera_tcp_connect_num = 0;//接收 readyRead()信号的触发计数
    static int camera_image_num = 0;	//接收到的图像数据的计数
    camera_tcp_connect_num++;
    QString rCount = QString::number(camera_tcp_connect_num);
    ui->connect_num->setText(rCount);

    QByteArray message;//存放从服务器接收到的字节流数据
    QDataStream in(camera_client_socket);	//将客户端套接字与输入数据流对象in绑定

    in.setVersion(QDataStream::Qt_5_10);//设置数据流的版本

    /*接收端的 这部分控制逻辑很重要*/

    if (camera_imageBlockSize == 0)
    {
        //如果imageBlockSize == 0 则说明,一幅图像的大小信息还未传输过来

        //uint64是8字节的8 Bytes  64bit
        //判断接收的数据是否有8字节（文件大小信息）
        //如果有则保存到basize变量中，没有则返回，继续接收数据
        if (camera_client_socket->bytesAvailable() < (int)sizeof(quint64))
        {//一幅图像的大小信息还未传输过来
            return;
        }
        in >> camera_imageBlockSize;//一幅图像的大小信息


        if (camera_imageBlockSize == (qint64)(0xFFFFFFFFFFFFFFFF))//视频结束的标注符
        {
            camera_client_socket->close();
            QMessageBox::information(this, tr("warning"), tr("the video is end!"));
            return;
        }

        qDebug() << "imageBlockSize  is " << camera_imageBlockSize;
        QString imageBlockS = "imageBlockSize  is " + QString::number(camera_imageBlockSize) + "Bytes!";
        //ui->command_info->addItem(imageBlockS);
        message.resize(camera_imageBlockSize);

    }
    //如果没有得到一幅图像的全部数据，则返回继续接收数据
    if (camera_client_socket->bytesAvailable() < camera_imageBlockSize)
    {
        return;
    }

    in >> message;//一幅图像所有像素的完整字节流

    camera_imageBlockSize = 0;//已经收到一幅完整的图像，将imageBlockSize置0，等待接收下一幅图像
    camera_image_num++;	//已接收的图像计数

    QString iCount = QString::number(camera_image_num);
    ui->img_num->setText(iCount);

    camera_cilent_showimage(message);	//显示当前接收到的这一幅图像

}
void MainWindow::camera_cilent_showimage(QByteArray tcp_img)	//从接收到了字节流中，执行与服务器断相反的操作：解压缩、解释为图像数据
{
    QString ss = QString::fromLatin1(tcp_img.data(), tcp_img.size());
    QByteArray rc;
    rc = QByteArray::fromBase64(ss.toLatin1());
    QByteArray rdc = qUncompress(rc);
    QImage img;
    //img.loadFromData(rdc,"JPEG");//解释为jpg格式的图像
    img.loadFromData(rdc);//解释为jpg格式的图像
    img_save =img.copy();

    //qDebug()<<img_save.format();

    img_video = Mat(img_save.height(), img_save.width(), CV_8UC4, (void*)img_save.bits(), img_save.bytesPerLine());  //转换格式

//    //imshow("sds",img_video);

//    cvtColor(img_video, img_video, COLOR_RGB2BGR);
      cvtColor(img_video, img_video, COLOR_BGR2RGB);
//    //因为opencv中默认图像通道顺序为BGR，而Qt中默认图像颜色通道为RGB，因此转换过程中，需要对通道顺序进行调整：

    if (img_video_state==true)
    {
    video_save.write(img_video);

    }

    ui->camera_img_show->setPixmap(QPixmap::fromImage(img));

    //ui->camera_img_show->resize(img.size());
    //update();
}

void MainWindow::camera_client_Disconnected()
{
    //ui->command_info->addItem("Error:Camera Connection closed by server!");
    camera_client_socket->close();//关闭客户端套接字
    ui->camera_client_connectButton->setEnabled(true);
}

void MainWindow::on_tcp_camera_requestButton_clicked()
{
    QByteArray requestMessage;	//请求消息（字节数组）
    QDataStream out(&requestMessage, QIODevice::WriteOnly);//只读输出流
    out.setVersion(QDataStream::Qt_5_10);
    out << quint16(0) << quint8('R');//将请求消息的大小、长度（字节数）与请求消息 'R'写入 输出数据流out
    out.device()->seek(0);
    out << quint16(requestMessage.size() - sizeof(quint16));

    camera_client_socket->write(requestMessage);//将输出数据流中的数据写入套接字

    //ui->command_info->addItem("Sending request...");

    ui->tcp_camera_requestButton->setEnabled(false);

}

void MainWindow::on_camera_server_listen_clicked()
{
    if(ui->camera_server_listen->text() == tr("应答请求"))
    {

        if(!camera_server->listen(QHostAddress::Any, 8888))
        {
            //若出错，则输出错误信息
            qDebug()<<camera_server->errorString();
           // ui->command_info->addItem(camera_server->errorString());
            //QMessageBox::critical(this, tr("Fortune Server"), tr("Unable to start the server:%l.").arg(camera_server->errorString()));

            return;
        }
        //修改按键文字
        ui->camera_server_listen->setText("取消应答");
        qDebug()<< "Listen succeessfully!";
        //ui->command_info->addItem( "Listen succeessfully!");
        ui->camera_client_connectButton->setEnabled(false);
        //ui->open_video_Button->setEnabled(false);

    }
    else
    {
        //serversocket->abort();
        //取消侦听
        camera_server->close();
        //修改按键文字
        ui->camera_server_listen->setText("应答请求");
        //发送按键失能
        ui->camera_server_listen->setEnabled(true);

       // ui->tcp_connect_button->setEnabled(true);
        //ui->OpenSerialButton->setEnabled(true);
    }

}

void MainWindow::on_img_save_button_clicked()
{
    QDateTime time = QDateTime::currentDateTime();
    QString dateTime = time.toString("MM-dd-hh-mm-ss");
    QString str= QString("Data%1.dat").arg(dateTime);

    img_save.save("./imgout/"+str+".JPG");
}

void MainWindow::on_save_data_clicked()
{

    if(ui->save_data->text() == tr("开始保存"))
    {
    QDateTime time = QDateTime::currentDateTime();
    QString dateTime = time.toString("MM-dd-hh-mm-ss");
    QString time_str= QString("Data%1.dat").arg(dateTime);

    //QFile file("./str.txt");

    QString fileName = "./data_out/"+time_str+"data.txt";
    //QString str = "this is testing for save data to txt file by Qt programming.";

    data_file.setFileName(fileName);
    data_file.open(QIODevice::ReadWrite);

    //定时器 Timer_data_save
    Timer_data_save = new QTimer(this);
    connect(Timer_data_save,SIGNAL(timeout()),this,SLOT(Timer_data_save_Update()));

    Timer_data_save->start(50);

     ui->save_data->setText("结束保存");
    }
    else
    {
      ui->save_data->setText("开始保存");
      Timer_data_save->stop();
      data_file.close();
    }

}

void MainWindow::Timer_data_save_Update()  //数据保存定时器
{
    QDateTime time = QDateTime::currentDateTime();
    QString dateTime = time.toString("MM-dd-hh-mm-ss");
    QString time_str= QString("Data%1.dat").arg(dateTime);

    QTextStream stream(&data_file);
    stream<<time_str<< ":"<< currrnt_vx << " "<< currrnt_vy<< " "<< currrnt_vz<< endl;
}


void MainWindow::on_video_save_button_clicked()  //视频保存
{
   if(ui->video_save_button->text() == tr("开始"))
  {

    QDateTime time = QDateTime::currentDateTime();
    QString dateTime = time.toString("MM-dd-hh-mm-ss");
    QString time_str= QString("%1").arg(dateTime);

    //QFile file("./str.txt");

    QString video_Name = "./video_output/"+time_str+"_weld.avi";
    //QString str = "this is testing for save data to txt file by Qt programming.";
    video_save=VideoWriter(qPrintable(video_Name), VideoWriter::fourcc('M', 'J', 'P', 'G'), 10.0,Size(640,240),true);

    ui->video_save_button->setText("结束");

    img_video_state=true;

   }

   else
   {
       ui->video_save_button->setText("开始");
//       video_save.release();
       img_video_state=false;
   }
}

void MainWindow::on_reset_clicked()
{
    ui->widget->LMove = QPoint(0,0);
    ui->widget->MMove = QPoint(0,0);
}

void MainWindow::on_comboBox_activated(int index)
{
    if(index == 0)
        ui->widget->modeflag = 0;
    else if(index == 1)
        ui->widget->modeflag =1;
}
```

---

## 6. mainwindow.ui（主窗口 UI 布局）

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>MainWindow</class>
 <widget class="QMainWindow" name="MainWindow">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>1900</width>
    <height>849</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>MainWindow</string>
  </property>
  <widget class="QWidget" name="centralwidget">
   <widget class="QLabel" name="label_24">
    <property name="geometry">
     <rect>
      <x>10</x>
      <y>670</y>
      <width>259</width>
      <height>27</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>18</pointsize>
     </font>
    </property>
    <property name="text">
     <string>Command:</string>
    </property>
   </widget>
   <widget class="QTextEdit" name="Command_text">
    <property name="geometry">
     <rect>
      <x>10</x>
      <y>700</y>
      <width>1311</width>
      <height>111</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>15</pointsize>
     </font>
    </property>
    <property name="styleSheet">
     <string notr="true">background-color: rgb(85, 87, 83);
background-color: rgb(0, 0, 0);
color: rgb(255, 255, 255);

</string>
    </property>
    <property name="frameShape">
     <enum>QFrame::StyledPanel</enum>
    </property>
    <property name="readOnly">
     <bool>true</bool>
    </property>
   </widget>
   <widget class="openglwidget" name="widget" native="true">
    <property name="geometry">
     <rect>
      <x>1340</x>
      <y>20</y>
      <width>541</width>
      <height>661</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>15</pointsize>
     </font>
    </property>
   </widget>
   <widget class="QPushButton" name="reset">
    <property name="geometry">
     <rect>
      <x>1660</x>
      <y>720</y>
      <width>181</width>
      <height>61</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>18</pointsize>
     </font>
    </property>
    <property name="text">
     <string>重置视角</string>
    </property>
   </widget>
   <widget class="QGroupBox" name="groupBox">
    <property name="geometry">
     <rect>
      <x>10</x>
      <y>0</y>
      <width>311</width>
      <height>351</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>18</pointsize>
     </font>
    </property>
    <property name="title">
     <string>数据通讯</string>
    </property>
    <widget class="QPushButton" name="tcp_connect_button">
     <property name="geometry">
      <rect>
       <x>20</x>
       <y>170</y>
       <width>271</width>
       <height>161</height>
      </rect>
     </property>
     <property name="sizePolicy">
      <sizepolicy hsizetype="Minimum" vsizetype="Fixed">
       <horstretch>0</horstretch>
       <verstretch>0</verstretch>
      </sizepolicy>
     </property>
     <property name="font">
      <font>
       <pointsize>18</pointsize>
      </font>
     </property>
     <property name="styleSheet">
      <string notr="true">background-color: rgb(0, 255, 0);</string>
     </property>
     <property name="text">
      <string>连接机器人</string>
     </property>
    </widget>
    <widget class="QWidget" name="">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>36</y>
       <width>85</width>
       <height>107</height>
      </rect>
     </property>
     <layout class="QVBoxLayout" name="verticalLayout">
      <item>
       <widget class="QLabel" name="label_19">
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="text">
         <string>本机IP:</string>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QLabel" name="label_17">
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="text">
         <string>机器人IP:</string>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QLabel" name="label_18">
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="text">
         <string>端口号：</string>
        </property>
       </widget>
      </item>
     </layout>
    </widget>
    <widget class="QWidget" name="">
     <property name="geometry">
      <rect>
       <x>110</x>
       <y>36</y>
       <width>178</width>
       <height>107</height>
      </rect>
     </property>
     <layout class="QVBoxLayout" name="verticalLayout_2">
      <item>
       <widget class="QLineEdit" name="host_IP">
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="readOnly">
         <bool>true</bool>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QLineEdit" name="Server_IP">
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QLineEdit" name="lineEdit_Port">
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
       </widget>
      </item>
     </layout>
    </widget>
   </widget>
   <widget class="QGroupBox" name="groupBox_6">
    <property name="geometry">
     <rect>
      <x>330</x>
      <y>0</y>
      <width>521</width>
      <height>351</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>18</pointsize>
     </font>
    </property>
    <property name="title">
     <string>运行状态</string>
    </property>
    <widget class="QGroupBox" name="groupBox_4">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>40</y>
       <width>281</width>
       <height>141</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="title">
      <string>机器人速度:</string>
     </property>
     <widget class="QWidget" name="">
      <property name="geometry">
       <rect>
        <x>10</x>
        <y>30</y>
        <width>261</width>
        <height>109</height>
       </rect>
      </property>
      <layout class="QHBoxLayout" name="horizontalLayout">
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_3">
         <item>
          <widget class="QLabel" name="label_8">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Vx 速度:</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_9">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Vy 速度:</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_10">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Va 角速度:</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_5">
         <item>
          <widget class="QLineEdit" name="current_vx_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="current_vy_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="current_vz_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_4">
         <item>
          <widget class="QLabel" name="label_26">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>m/s</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_25">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>m/s</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_27">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>rad/s</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
      </layout>
     </widget>
    </widget>
    <widget class="QGroupBox" name="groupBox_10">
     <property name="geometry">
      <rect>
       <x>300</x>
       <y>40</y>
       <width>211</width>
       <height>141</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="title">
      <string>机器人角度:</string>
     </property>
     <widget class="QWidget" name="formLayoutWidget_6">
      <property name="geometry">
       <rect>
        <x>10</x>
        <y>30</y>
        <width>191</width>
        <height>107</height>
       </rect>
      </property>
      <property name="font">
       <font>
        <pointsize>15</pointsize>
       </font>
      </property>
      <layout class="QGridLayout" name="gridLayout_4">
       <item row="0" column="2">
        <widget class="QLabel" name="label_35">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>°</string>
         </property>
        </widget>
       </item>
       <item row="2" column="0">
        <widget class="QLabel" name="label_36">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>俯仰角:</string>
         </property>
        </widget>
       </item>
       <item row="1" column="0">
        <widget class="QLabel" name="label_37">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>翻滚角:</string>
         </property>
        </widget>
       </item>
       <item row="2" column="1">
        <widget class="QLineEdit" name="current_vz_text_3">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="readOnly">
          <bool>true</bool>
         </property>
        </widget>
       </item>
       <item row="0" column="1">
        <widget class="QLineEdit" name="current_vx_text_3">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="readOnly">
          <bool>true</bool>
         </property>
        </widget>
       </item>
       <item row="0" column="0">
        <widget class="QLabel" name="label_38">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>航向角:</string>
         </property>
        </widget>
       </item>
       <item row="1" column="1">
        <widget class="QLineEdit" name="current_vy_text_3">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="readOnly">
          <bool>true</bool>
         </property>
        </widget>
       </item>
       <item row="1" column="2">
        <widget class="QLabel" name="label_39">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>°</string>
         </property>
        </widget>
       </item>
       <item row="2" column="2">
        <widget class="QLabel" name="label_40">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>°</string>
         </property>
        </widget>
       </item>
      </layout>
     </widget>
    </widget>
    <widget class="QGroupBox" name="groupBox_9">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>180</y>
       <width>281</width>
       <height>161</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="title">
      <string>机器人空间坐标:</string>
     </property>
     <widget class="QWidget" name="">
      <property name="geometry">
       <rect>
        <x>5</x>
        <y>31</y>
        <width>261</width>
        <height>121</height>
       </rect>
      </property>
      <layout class="QHBoxLayout" name="horizontalLayout_2">
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_6">
         <item>
          <widget class="QLabel" name="label_41">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>X坐标:</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_42">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Y坐标:</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_43">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Z坐标:</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_7">
         <item>
          <widget class="QLineEdit" name="lineEdit_7">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="lineEdit_8">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="lineEdit_9">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_8">
         <item>
          <widget class="QLabel" name="label_44">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>m</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_45">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>m</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_46">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>m</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
      </layout>
     </widget>
    </widget>
    <widget class="QGroupBox" name="groupBox_3">
     <property name="geometry">
      <rect>
       <x>300</x>
       <y>180</y>
       <width>211</width>
       <height>161</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="title">
      <string>巡线状态</string>
     </property>
     <widget class="QWidget" name="">
      <property name="geometry">
       <rect>
        <x>10</x>
        <y>31</y>
        <width>191</width>
        <height>121</height>
       </rect>
      </property>
      <layout class="QHBoxLayout" name="horizontalLayout_3">
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_9">
         <item>
          <widget class="QLabel" name="label_11">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>偏角 a:</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_12">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>偏距 d:</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_13">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>标志位:</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_10">
         <item>
          <widget class="QLineEdit" name="weld_angle_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="weld_distance_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="weld_state_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="readOnly">
            <bool>true</bool>
           </property>
          </widget>
         </item>
        </layout>
       </item>
      </layout>
     </widget>
    </widget>
   </widget>
   <widget class="QGroupBox" name="groupBox_11">
    <property name="geometry">
     <rect>
      <x>10</x>
      <y>340</y>
      <width>1311</width>
      <height>341</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>18</pointsize>
     </font>
    </property>
    <property name="title">
     <string>实时监控（端口号：9090/9999）</string>
    </property>
    <widget class="QLabel" name="camera_img_show">
     <property name="geometry">
      <rect>
       <x>380</x>
       <y>60</y>
       <width>640</width>
       <height>240</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="frameShape">
      <enum>QFrame::Panel</enum>
     </property>
     <property name="text">
      <string>wait images...</string>
     </property>
    </widget>
    <widget class="QWidget" name="">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>40</y>
       <width>302</width>
       <height>151</height>
      </rect>
     </property>
     <layout class="QHBoxLayout" name="horizontalLayout_5">
      <item>
       <layout class="QVBoxLayout" name="verticalLayout_13">
        <item>
         <widget class="QLabel" name="label_32">
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>机器人摄像头：</string>
          </property>
         </widget>
        </item>
        <item>
         <layout class="QHBoxLayout" name="horizontalLayout_4">
          <item>
           <layout class="QVBoxLayout" name="verticalLayout_11">
            <item>
             <widget class="QLabel" name="label_33">
              <property name="font">
               <font>
                <pointsize>15</pointsize>
               </font>
              </property>
              <property name="text">
               <string>通讯：</string>
              </property>
             </widget>
            </item>
            <item>
             <widget class="QLabel" name="label_29">
              <property name="font">
               <font>
                <pointsize>15</pointsize>
               </font>
              </property>
              <property name="text">
               <string>图像：</string>
              </property>
             </widget>
            </item>
           </layout>
          </item>
          <item>
           <layout class="QVBoxLayout" name="verticalLayout_12">
            <item>
             <widget class="QLabel" name="connect_num">
              <property name="font">
               <font>
                <pointsize>15</pointsize>
               </font>
              </property>
              <property name="text">
               <string>0</string>
              </property>
             </widget>
            </item>
            <item>
             <widget class="QLabel" name="img_num">
              <property name="font">
               <font>
                <pointsize>15</pointsize>
               </font>
              </property>
              <property name="text">
               <string>0</string>
              </property>
             </widget>
            </item>
           </layout>
          </item>
         </layout>
        </item>
       </layout>
      </item>
      <item>
       <layout class="QVBoxLayout" name="verticalLayout_14">
        <item>
         <widget class="QPushButton" name="camera_client_connectButton">
          <property name="minimumSize">
           <size>
            <width>150</width>
            <height>100</height>
           </size>
          </property>
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="styleSheet">
           <string notr="true">background-color: rgb(0, 255, 0);</string>
          </property>
          <property name="text">
           <string>开启连接</string>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QPushButton" name="tcp_camera_requestButton">
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>请求远程图像</string>
          </property>
         </widget>
        </item>
       </layout>
      </item>
     </layout>
    </widget>
    <widget class="QWidget" name="">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>190</y>
       <width>301</width>
       <height>133</height>
      </rect>
     </property>
     <layout class="QHBoxLayout" name="horizontalLayout_6">
      <item>
       <layout class="QVBoxLayout" name="verticalLayout_15">
        <item>
         <widget class="QLabel" name="label_30">
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>数据保存：</string>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QLabel" name="label_47">
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>图像保存：</string>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QLabel" name="label_31">
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>视频保存：</string>
          </property>
         </widget>
        </item>
       </layout>
      </item>
      <item>
       <layout class="QVBoxLayout" name="verticalLayout_16">
        <item>
         <widget class="QPushButton" name="save_data">
          <property name="minimumSize">
           <size>
            <width>150</width>
            <height>0</height>
           </size>
          </property>
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>开始保存</string>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QPushButton" name="img_save_button">
          <property name="minimumSize">
           <size>
            <width>150</width>
            <height>0</height>
           </size>
          </property>
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>保存图像</string>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QPushButton" name="video_save_button">
          <property name="minimumSize">
           <size>
            <width>150</width>
            <height>0</height>
           </size>
          </property>
          <property name="font">
           <font>
            <pointsize>15</pointsize>
           </font>
          </property>
          <property name="text">
           <string>开始</string>
          </property>
         </widget>
        </item>
       </layout>
      </item>
     </layout>
    </widget>
    <widget class="QWidget" name="">
     <property name="geometry">
      <rect>
       <x>1060</x>
       <y>50</y>
       <width>211</width>
       <height>260</height>
      </rect>
     </property>
     <layout class="QVBoxLayout" name="verticalLayout_26">
      <item>
       <widget class="QPushButton" name="pushButton_3">
        <property name="minimumSize">
         <size>
          <width>150</width>
          <height>50</height>
         </size>
        </property>
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="styleSheet">
         <string notr="true">background-color: rgb(32, 74, 135);</string>
        </property>
        <property name="text">
         <string>功能1</string>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QPushButton" name="pushButton_5">
        <property name="minimumSize">
         <size>
          <width>150</width>
          <height>50</height>
         </size>
        </property>
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="styleSheet">
         <string notr="true">background-color: rgb(32, 74, 135);</string>
        </property>
        <property name="text">
         <string>功能2</string>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QPushButton" name="pushButton_6">
        <property name="minimumSize">
         <size>
          <width>150</width>
          <height>50</height>
         </size>
        </property>
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="styleSheet">
         <string notr="true">background-color: rgb(32, 74, 135);</string>
        </property>
        <property name="text">
         <string>功能3</string>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QPushButton" name="pushButton_7">
        <property name="minimumSize">
         <size>
          <width>150</width>
          <height>50</height>
         </size>
        </property>
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="styleSheet">
         <string notr="true">background-color: rgb(32, 74, 135);</string>
        </property>
        <property name="text">
         <string>功能4</string>
        </property>
       </widget>
      </item>
     </layout>
    </widget>
   </widget>
   <widget class="QGroupBox" name="groupBox_8">
    <property name="geometry">
     <rect>
      <x>860</x>
      <y>0</y>
      <width>461</width>
      <height>351</height>
     </rect>
    </property>
    <property name="font">
     <font>
      <pointsize>18</pointsize>
     </font>
    </property>
    <property name="title">
     <string>robot 控制</string>
    </property>
    <widget class="QGroupBox" name="groupBox_2">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>170</y>
       <width>171</width>
       <height>171</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="title">
      <string>robot  状态选择</string>
     </property>
     <widget class="QWidget" name="">
      <property name="geometry">
       <rect>
        <x>10</x>
        <y>40</y>
        <width>151</width>
        <height>125</height>
       </rect>
      </property>
      <layout class="QVBoxLayout" name="verticalLayout_18">
       <item>
        <widget class="QRadioButton" name="radioButton">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>遥控运行</string>
         </property>
         <attribute name="buttonGroup">
          <string notr="true">buttonGroup</string>
         </attribute>
        </widget>
       </item>
       <item>
        <widget class="QRadioButton" name="radioButton_2">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>命令控制</string>
         </property>
         <attribute name="buttonGroup">
          <string notr="true">buttonGroup</string>
         </attribute>
        </widget>
       </item>
       <item>
        <widget class="QRadioButton" name="radioButton_3">
         <property name="font">
          <font>
           <pointsize>15</pointsize>
          </font>
         </property>
         <property name="text">
          <string>巡线运行</string>
         </property>
         <attribute name="buttonGroup">
          <string notr="true">buttonGroup</string>
         </attribute>
        </widget>
       </item>
      </layout>
     </widget>
    </widget>
    <widget class="QPushButton" name="camera_server_listen">
     <property name="geometry">
      <rect>
       <x>10</x>
       <y>130</y>
       <width>431</width>
       <height>39</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="text">
      <string>应答请求</string>
     </property>
    </widget>
    <widget class="QGroupBox" name="groupBox_5">
     <property name="geometry">
      <rect>
       <x>190</x>
       <y>170</y>
       <width>251</width>
       <height>171</height>
      </rect>
     </property>
     <property name="font">
      <font>
       <pointsize>15</pointsize>
      </font>
     </property>
     <property name="title">
      <string>robot 速度命令(16进制)</string>
     </property>
     <widget class="QWidget" name="">
      <property name="geometry">
       <rect>
        <x>11</x>
        <y>30</y>
        <width>231</width>
        <height>131</height>
       </rect>
      </property>
      <layout class="QHBoxLayout" name="horizontalLayout_8">
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_17">
         <item>
          <widget class="QLabel" name="label_6">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>X方向速度</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_7">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Y方向速度</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLabel" name="label_16">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>Z方向速度</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
       <item>
        <layout class="QVBoxLayout" name="verticalLayout_19">
         <item>
          <widget class="QLineEdit" name="control_vx_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>0</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="control_vy_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>0</string>
           </property>
          </widget>
         </item>
         <item>
          <widget class="QLineEdit" name="control_vz_text">
           <property name="font">
            <font>
             <pointsize>15</pointsize>
            </font>
           </property>
           <property name="text">
            <string>0</string>
           </property>
          </widget>
         </item>
        </layout>
       </item>
      </layout>
     </widget>
    </widget>
    <widget class="QWidget" name="">
     <property name="geometry">
      <rect>
       <x>11</x>
       <y>40</y>
       <width>431</width>
       <height>82</height>
      </rect>
     </property>
     <layout class="QHBoxLayout" name="horizontalLayout_7">
      <item>
       <widget class="QPushButton" name="pushButton_2">
        <property name="minimumSize">
         <size>
          <width>120</width>
          <height>80</height>
         </size>
        </property>
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="styleSheet">
         <string notr="true">background-color: rgb(115, 210, 22);</string>
        </property>
        <property name="text">
         <string>使能</string>
        </property>
       </widget>
      </item>
      <item>
       <widget class="QPushButton" name="pushButton">
        <property name="minimumSize">
         <size>
          <width>120</width>
          <height>80</height>
         </size>
        </property>
        <property name="font">
         <font>
          <pointsize>15</pointsize>
         </font>
        </property>
        <property name="styleSheet">
         <string notr="true">background-color: rgb(239, 41, 41);</string>
        </property>
        <property name="text">
         <string>停车</string>
        </property>
       </widget>
      </item>
     </layout>
    </widget>
   </widget>
   <widget class="QWidget" name="">
    <property name="geometry">
     <rect>
      <x>1360</x>
      <y>720</y>
      <width>271</width>
      <height>61</height>
     </rect>
    </property>
    <layout class="QHBoxLayout" name="horizontalLayout_9">
     <item>
      <widget class="QLabel" name="label_34">
       <property name="font">
        <font>
         <pointsize>18</pointsize>
        </font>
       </property>
       <property name="text">
        <string>罐类型：</string>
       </property>
      </widget>
     </item>
     <item>
      <widget class="QComboBox" name="comboBox">
       <property name="minimumSize">
        <size>
         <width>100</width>
         <height>50</height>
        </size>
       </property>
       <property name="font">
        <font>
         <pointsize>18</pointsize>
        </font>
       </property>
       <item>
        <property name="text">
         <string>柱罐</string>
        </property>
       </item>
       <item>
        <property name="text">
         <string>球罐</string>
        </property>
       </item>
      </widget>
     </item>
    </layout>
   </widget>
  </widget>
  <widget class="QStatusBar" name="statusbar"/>
  <widget class="QMenuBar" name="menubar">
   <property name="geometry">
    <rect>
     <x>0</x>
     <y>0</y>
     <width>1900</width>
     <height>22</height>
    </rect>
   </property>
  </widget>
 </widget>
 <customwidgets>
  <customwidget>
   <class>openglwidget</class>
   <extends>QWidget</extends>
   <header>openglwidget.h</header>
   <container>1</container>
  </customwidget>
 </customwidgets>
 <resources/>
 <connections/>
 <buttongroups>
  <buttongroup name="buttonGroup"/>
 </buttongroups>
</ui>
```

---

## 7. openglwidget.h（OpenGL 3D 渲染组件头文件）

```cpp
#ifndef OPENGLWIDGET_H
#define OPENGLWIDGET_H

#include <QWidget>
#include <QOpenGLWidget>
#include <GL/gl.h>
#include <GL/glu.h>
#include <QDebug>
#include <QEvent>
#include <config.h>
#include <vector>
#include <QMouseEvent>
#include <QTimer>
#include <QFile>
namespace Ui {
class openglwidget;
}

class openglwidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit openglwidget(QWidget *parent = 0);
    ~openglwidget();
    std::vector<float> ptofinrob_x,ptofinrob_y,ptofinrob_z;   //将接受到的数据存在该数据组即可显示
    QPoint LMove,MMove;  //用于记录鼠标移动过程中的点的坐标
    int modeflag=0; //模型标志，-1 表示未加载模型，0 表示加载圆柱罐模型  1 表示加载球罐模型
    bool InitMyOpenGL(void);
private:
    Ui::openglwidget *ui;
    QTimer* Timer_plot;

    SpherePT cylPt[CYLINDERPTNUM];
    int changelineflag[3];  //记录下在第几个点，开始绘制下一条直线;
    SphereWeldseam cylWeld[CYLINDERCROSSPTNUM][CYLINDERCROSSPTNUM];
    SphereCrossPT cylcrossPt[CYLINDERCROSSPTNUM];
    SphereRoute cylroute[CYLINDERROUTNUM];
    int numofweldptco=0;
    int numofweldptsp=0;
    QPoint P_Point,C_Point;
    float angle[3];

    //球罐相关变量
    SpherePT sphPt[SPHEREPTNUM];
    SphereCrossPT sphcrossPt[SPHERECROSSPTNUM];
    SphereWeldseam sphweld[SPHERECROSSPTNUM][SPHERECROSSPTNUM];
    SphereRoute sphroute[SPHEREROUTNUM];
    int linesnum;
    SphereRouteNum sphroutenum[80];


    bool mouseDown;
private slots:
    void dealTimeOut();
protected:
    void initializeGL();
    void resizeGL(int w, int h);
    void paintGL();


    void drawxyz();
    int InitOfCylinder();//得到圆柱罐的点，并返回点的个数
    void InitWeldEdgeOfCylinder(void);//圆柱罐上任意两点的斜率 ，0 表示经线，1表示纬线，其他  2
    void InitCylinderWeldRoute(void);     //交叉点
    void DrawWeldLinesOfCylinder(void);//画出焊缝轨迹的线
    void DrawMyCylinder(void);    //画圆柱并制作动画
    void DrawPTofInrobSph(void);    //显示机器人走过的点

    //球罐相关
    int InitPtOfSphere(void);
    void InitWeldEdgeOfSphere();
    void InitSphereWeldRoute();
    void DrawWeldLinesOfSphere();
    bool DrawMySphere();

    virtual void mousePressEvent(QMouseEvent *ev);
    virtual void mouseReleaseEvent(QMouseEvent *ev);
    virtual void mouseMoveEvent(QMouseEvent *ev);
};

#endif // OPENGLWIDGET_H
```

---

## 8. openglwidget.cpp（OpenGL 3D 渲染实现）

```cpp
#include "openglwidget.h"
#include "ui_openglwidget.h"
#define tank_R 1.9

openglwidget::openglwidget(QWidget *parent) :
    QOpenGLWidget(parent),
    ui(new Ui::openglwidget)
{
    ui->setupUi(this);
    mouseDown = false;
    Timer_plot = new QTimer(this);
    connect(Timer_plot,SIGNAL(timeout()),this,SLOT(dealTimeOut()));
}

openglwidget::~openglwidget()
{
    Timer_plot->stop();
    delete ui;
}

void openglwidget::initializeGL()
{
    glClearColor(1,1,1,0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    InitMyOpenGL();
    Timer_plot->start(100);

}
bool openglwidget::InitMyOpenGL(){
        MMove.setX(0);//初始化初始角度
        MMove.setY(0);
        //设定光源以及背景颜色
        GLfloat AmbientLight[4]={1,1,1,1};
        GLfloat light_position [ ] = { 1.0, -1.0, 1.0, -1.0 };
        GLfloat mat_diffuse[] = { 0.9, 0.9, 0.9, 1.0 };
        GLfloat mat_Ambient[] = { 0.1, 0.1, 0.1, 1.0 };
        glClearColor ( 1.0, 1.0, 1.0, 1.0 ); //设置背景色为蓝色
        glShadeModel ( GL_SMOOTH );    //着色方式为渐变色

        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diffuse);   //指定用于光照计算的当前材质属性
        glMaterialfv(GL_LIGHT0,  GL_AMBIENT,  mat_Ambient);

        glLightfv ( GL_LIGHT0, GL_POSITION, light_position);   //指定0号光源的位置
        glLightfv(GL_LIGHT0,  GL_DIFFUSE,  mat_diffuse);
        glLightfv(GL_LIGHT0,  GL_AMBIENT,  AmbientLight);

        glEnable (GL_LIGHTING);    //使用光照
        glEnable (GL_LIGHT0);   //使用光源0
        glEnable (GL_DEPTH_TEST);   //启用深度测试
        glEnable(GL_COLOR_MATERIAL); //启用颜色跟踪

        glOrtho(-2.5, 2.5, -2.5, 2.5, -3, 3);  //圆柱罐的坐标系
        numofweldptco=InitOfCylinder();       //将焊缝的点存储到数组中   圆柱罐
        InitWeldEdgeOfCylinder();
        InitCylinderWeldRoute();

        glOrtho(-2.5, 2.5, -2.5, 2.5, -3, 3);  //圆柱罐的坐标系
        numofweldptsp=InitPtOfSphere();       //将焊缝的点存储到数组中   圆柱罐
        InitWeldEdgeOfSphere();
        InitSphereWeldRoute();
        return true;
}
void openglwidget::paintGL()
{
    if(modeflag == 0)
        DrawMyCylinder();
    else if(modeflag == 1)
        DrawMySphere();
}

void openglwidget::resizeGL(int w, int h)
{
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45, (float)w/h, 0.01, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void openglwidget::drawxyz(void)  //在图中画出x,y,z坐标系
{
    glColor3f(1.f, 0.f, 0.f);
    glLineWidth(2.f);
    glBegin(GL_LINES);
    glVertex3f(0.0f, 0.0f, 0.f);
    glVertex3f(20.0, 0.0f, 0.f);
    glEnd();
    QString a="x";
    glRasterPos3f(1.1,0.f,0.f);//控制显示位置

    glBegin(GL_LINES);
    glVertex3f(0.0f, 0.0f, 0.f);
    glVertex3f(0.f, 20.0f, 0.f);
    glEnd();
    a='y';
    glRasterPos3f(0.f,1.1f,0.f);//控制显示位置

    glBegin(GL_LINES);
    glVertex3f(0.0f, 0.0f, 0.f);
    glVertex3f(0.f, 0.0f, 20.f);
    glEnd();
    a='z';
    glRasterPos3f(0.f,0.f,1.1f);//控制显示位置

}
int openglwidget::InitOfCylinder(){
    float angle=0;
        int num=0;
        while(angle<=360)   //经线
        {
            cylPt[num].angle1=angle;
            cylPt[num].x=tank_R*cos(cylPt[num].angle1/180*3.14);
            cylPt[num].y=tank_R*sin(angle/180*3.14);
            cylPt[num].z=1.f;
            cylPt[num].angle2=cylPt[num].z;
            angle+=1.0/NUMPERDEG;
            num++;
        }
        changelineflag[0]=num;
        float tmpx=tank_R*cosf(94/180.0*3.14);
        float tmpy=tank_R*sinf(94/180.0*3.14);
        float tmpz=1.f;
        while(tmpz<2.8)     //2->1
        {
            cylPt[num].x=tmpx;
            cylPt[num].y=tmpy;
            cylPt[num].z=tmpz;
            cylPt[num].angle1=94; //94/180.0*3.14;
            cylPt[num].angle2=cylPt[num].z;

            tmpz+=0.0085;
            num++;
        }
        changelineflag[1]=num;

        tmpx=-tank_R;
        tmpy=0.f;
        tmpz=0.f;
        while(tmpz<2.8)  //5->4->3
        {
            cylPt[num].x=tmpx;
            cylPt[num].y=tmpy;
            cylPt[num].z=tmpz;
            cylPt[num].angle1=180;//180°
            cylPt[num].angle2=cylPt[num].z;
            tmpz+=0.0085;
            num++;
        }
        changelineflag[2]=num;
        tmpx=tank_R*cosf(266/180.0*3.14);;
        tmpy=tank_R*sinf(266/180.0*3.14);
        tmpz=tank_R;
        while(tmpz<1.0)       //7->6
        {
            cylPt[num].x=tmpx;
            cylPt[num].y=tmpy;
            cylPt[num].z=tmpz;
            cylPt[num].angle1=266;//266/180.0*3.14;
            cylPt[num].angle2=cylPt[num].z;
            tmpz+=0.0085;
            num++;
        }
        return num;
}
void openglwidget::InitWeldEdgeOfCylinder(void)
{
    for(int i=0;i<CYLINDERCROSSPTNUM;i++)
    {
        for(int j=0;j<CYLINDERCROSSPTNUM;j++)
        {
            cylWeld[i][j].dig=2;
            cylWeld[j][i].dig=2;
            cylWeld[i][j].visitednum=0;
            cylWeld[j][i].visitednum=0;
        }
    }
    cylWeld[0][2].dig=0;	cylWeld[2][0].dig=0;
    cylWeld[2][4].dig=0;	cylWeld[4][2].dig=0;
    cylWeld[4][6].dig=0;	cylWeld[6][4].dig=0;
    cylWeld[6][0].dig=0;	cylWeld[0][6].dig=0;

    cylWeld[1][2].dig=1;	cylWeld[2][1].dig=1;
    cylWeld[3][4].dig=1;	cylWeld[4][3].dig=1;
    cylWeld[4][5].dig=1;	cylWeld[5][4].dig=1;
    cylWeld[6][7].dig=1;	cylWeld[7][6].dig=1;
}
void openglwidget::InitCylinderWeldRoute(){
    //交叉点    //在圆柱罐中 angle2 代表了高度
        for(int i=0;i<CYLINDERCROSSPTNUM;i++)
        {
            cylcrossPt[i].ptnum=i;
            cylcrossPt[i].endptnum[0]=-1;
            cylcrossPt[i].endptnum[1]=-1;
            cylcrossPt[i].endptnum[2]=-1;
        }
        cylcrossPt[0].angle1=0;		cylcrossPt[0].angle2=1;		cylcrossPt[0].endptnum[0]=2;	cylcrossPt[0].endptnum[1]=6;
        cylcrossPt[1].angle1=94;	cylcrossPt[1].angle2=2.8;	cylcrossPt[1].endptnum[0]=2;
        cylcrossPt[2].angle1=94;	cylcrossPt[2].angle2=1;		cylcrossPt[2].endptnum[0]=0;	cylcrossPt[2].endptnum[1]=1;	cylcrossPt[2].endptnum[2]=4;
        cylcrossPt[3].angle1=180;	cylcrossPt[3].angle2=2.8;	cylcrossPt[3].endptnum[0]=4;
        cylcrossPt[4].angle1=180;	cylcrossPt[4].angle2=1;		cylcrossPt[4].endptnum[0]=6;	cylcrossPt[4].endptnum[1]=3;	cylcrossPt[4].endptnum[2]=5;  ////有 4个边 省略了边 4-2
        cylcrossPt[5].angle1=180;	cylcrossPt[5].angle2=0;		cylcrossPt[5].endptnum[0]=4;
        cylcrossPt[6].angle1=266;	cylcrossPt[6].angle2=1;		cylcrossPt[6].endptnum[0]=4;	cylcrossPt[6].endptnum[1]=7;	cylcrossPt[6].endptnum[2]=0;
        cylcrossPt[7].angle1=266;	cylcrossPt[7].angle2=0;		cylcrossPt[7].endptnum[0]=6;

        //路径：
        for(int i=0;i<CYLINDERROUTNUM;i++)
        {
            cylroute[i].visitedtime=0;
        }
        cylroute[0].ptnum=0; cylroute[0].nextptnum=2; cylroute[0].dig=0;
        cylroute[1].ptnum=2; cylroute[1].nextptnum=1; cylroute[1].dig=1;
        cylroute[2].ptnum=1; cylroute[2].nextptnum=2; cylroute[2].dig=1; cylroute[2].visitedtime=1;
        cylroute[3].ptnum=2; cylroute[3].nextptnum=4; cylroute[3].dig=0;
        cylroute[4].ptnum=4; cylroute[4].nextptnum=3; cylroute[4].dig=1;
        cylroute[5].ptnum=3; cylroute[5].nextptnum=4; cylroute[5].dig=1; cylroute[5].visitedtime=1;
        cylroute[6].ptnum=4; cylroute[6].nextptnum=5; cylroute[6].dig=1;
        cylroute[7].ptnum=5; cylroute[7].nextptnum=4; cylroute[7].dig=1; cylroute[7].visitedtime=1;
        cylroute[8].ptnum=4; cylroute[8].nextptnum=6; cylroute[8].dig=0;
        cylroute[9].ptnum=6; cylroute[9].nextptnum=7; cylroute[9].dig=1;
        cylroute[10].ptnum=7; cylroute[10].nextptnum=6; cylroute[10].dig=1; cylroute[10].visitedtime=1;
        cylroute[11].ptnum=6; cylroute[11].nextptnum=0; cylroute[11].dig=0;
}
void openglwidget::DrawWeldLinesOfCylinder(){
    glColor3f(0.f,0.f,0.f);
    glLineWidth(4.f);
    for(int i=1;i<numofweldptco;i++)
    {
        if(i==changelineflag[0] || i==changelineflag[1] || i==changelineflag[2])
            continue;
        glBegin(GL_LINES);
        glVertex3f(cylPt[i-1].x,cylPt[i-1].y,cylPt[i-1].z);
        glVertex3f(cylPt[i].x,cylPt[i].y,cylPt[i].z);
        glEnd();
    }
}
void openglwidget::DrawPTofInrobSph(){
    int ptnum=ptofinrob_x.size();
    glPointSize(5.f);
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH,GL_NICEST);
    glColor3f(0.f,1.f,0.f);
    for(int i=0;i<ptnum-100;i++)
    {
        glBegin(GL_POINTS);
        glVertex3f(ptofinrob_x[i],ptofinrob_y[i],ptofinrob_z[i]);
        glEnd();
    }
    glPointSize(10.f);
    glColor3f(1.f,0.f,0.f);
    for(int i=((ptnum-100)>=0?(ptnum-100):0);i<ptnum;i++)
    {
        glBegin(GL_POINTS);
        glVertex3f(ptofinrob_x[i],ptofinrob_y[i],ptofinrob_z[i]);
        glEnd();
    }
}
void openglwidget::DrawMyCylinder(){
    //光源
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    angle[0] =MMove.x()/5;
//    angle[1] =-MMove.y()/5;
    glRotatef(angle[0], 0.0f, 0.f, 1.f);
//    glRotatef(angle[1], 1.0f, 0.f, 0.f);

    GLUquadricObj *quadratic;
    quadratic=gluNewQuadric();
    glPushMatrix();            //将当前矩阵压入矩阵堆栈
    glTranslatef(0,0,-1.4);   //平移到起点，即A点
    glColor3f(0.5, 0.5, 0.5);
    gluCylinder(quadratic,tank_R,tank_R,2.8f,32,32); //画圆柱
    //画坐标系x,y,z
    drawxyz();
    DrawWeldLinesOfCylinder();
    DrawPTofInrobSph();
    glPopMatrix();
    glFlush();
    glLoadIdentity();
    gluLookAt( 0, -8, 3, 0.0, 0.0, 0, 0, 0, 1);

}
//******************球罐***********************//
int openglwidget::InitPtOfSphere(void)
{
    int i=0;
    for( i=0;i<360*NUMPERDEG;i++)
    {
        sphPt[i].angle1=i*1.0/NUMPERDEG;
        sphPt[i].angle2=45.f; //上温带经线
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);

    }
    for(i=360*NUMPERDEG;i<720*NUMPERDEG;i++)
    {
        sphPt[i].angle1=(i-360*NUMPERDEG)*1.0/NUMPERDEG;
        sphPt[i].angle2=12.f; //赤道带经线
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);
    }

    for(i=720*NUMPERDEG;i<1080*NUMPERDEG;i++)
    {
        sphPt[i].angle1=(i-720*NUMPERDEG)*1.0/NUMPERDEG;
        sphPt[i].angle2=-45.f; //下极带经线
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);
    }

    float counter=45;
    for(i=1080*NUMPERDEG;i<1740*NUMPERDEG;i++)  //上温带的20条纬线  B-A
    {
        int n=(i-1080*NUMPERDEG)/33/NUMPERDEG;
        sphPt[i].angle1=n*18;   //经度 0 18 36 。。。。
        sphPt[i].angle2=counter; //纬度 12,13 。。45
        counter-=1.f/NUMPERDEG;
        if(counter<12)
        {
            counter= 45;
        }
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);
    }
    counter=12;
    for(int i=1740*NUMPERDEG;i<2880*NUMPERDEG;i++)  //赤道带的20条纬线 B-C
    {
        int n=(i-1740*NUMPERDEG)/57/NUMPERDEG;
        sphPt[i].angle1=n*18+9;   //经度 0 18 36 。。。。
        sphPt[i].angle2=counter;
        counter-=1.f/NUMPERDEG;
        if(counter<-45)
        {
            counter=12;
        }
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);


    }

    //上极带的点  从2880到3224
    counter=45;
    for(int i=2880*NUMPERDEG;i<2916*NUMPERDEG;i++)  //上极带 y=x  y=-x  大圆 纬线
    {
        int n=(i-2880*NUMPERDEG)/9/NUMPERDEG;
        sphPt[i].angle1= 90*n+45;
        sphPt[i].angle2=counter;
        counter+=1.f/NUMPERDEG;
        if(counter>54)
        {
            counter=45;
        }
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);
    }
    counter=65;
    for(int i=2916*NUMPERDEG;i<2966*NUMPERDEG;i++)  //上极 椭圆5  上半边
    {
        sphPt[i].x=7.85*cos(counter/180.f*3.14);
        sphPt[i].y=7.85*0.462*sin(counter/180.f*3.14);
        sphPt[i].z=7.85*0.887*sin(counter/180.f*3.14);
        sphPt[i].angle1=atan(sphPt[i].y/sphPt[i].x)/3.14*180;
        sphPt[i].angle2=asin(sphPt[i].z/7.85)/3.14*180;
        if(sphPt[i].x<0)
        {
            sphPt[i].angle1+=180;
        }
        else if(sphPt[i].y<0)
        {
            sphPt[i].angle1+=360;
        }
        counter+=1.f/NUMPERDEG;
    }

    counter=-115;
    for(int i=2966*NUMPERDEG;i<3016*NUMPERDEG;i++)  //上极 椭圆5  下半边   交叉点，斜率还没定义好。。。。
    {
        sphPt[i].x=7.85*cos(counter/180.f*3.14);
        sphPt[i].y=7.85*0.462*sin(counter/180.f*3.14);
        sphPt[i].z=-7.85*0.887*sin(counter/180.f*3.14);
        sphPt[i].angle1=atan(sphPt[i].y/sphPt[i].x)/3.14*180;
        sphPt[i].angle2=asin(sphPt[i].z/7.85)/3.14*180;
        if(sphPt[i].x<0)
        {
            sphPt[i].angle1+=180;
        }
        else if(sphPt[i].y<0)
        {
            sphPt[i].angle1+=360;
        }
        counter+=1.f/NUMPERDEG;
    }

    counter=-25;
    for(int i=3016*NUMPERDEG;i<3066*NUMPERDEG;i++)  //上极 椭圆6  右半边   交叉点还没定义好  斜率没定义。。。。
    {
        sphPt[i].x=7.85*0.462*cos(counter/180.f*3.14);
        sphPt[i].y=7.85*sin(counter/180.f*3.14);
        sphPt[i].z=fabs(7.85*0.887*cos(counter/180.f*3.14));
        sphPt[i].angle1=atan(sphPt[i].y/sphPt[i].x)/3.14*180;
        sphPt[i].angle2=asin(sphPt[i].z/7.85)/3.14*180;
        if(sphPt[i].x<0)
        {
            sphPt[i].angle1+=180;
        }
        else if(sphPt[i].y<0)
        {
            sphPt[i].angle1+=360;
        }


        counter+=1.f/NUMPERDEG;
    }

    counter=155;
    for(int i=3066*NUMPERDEG;i<3116*NUMPERDEG;i++)  //上极 椭圆6  左半边   交叉点还没定义好  斜率没定义。。。。
    {
        sphPt[i].x=7.85*0.462*cos(counter/180.f*3.14);
        sphPt[i].y=7.85*sin(counter/180.f*3.14);
        sphPt[i].z=fabs(7.85*0.887*cos(counter/180.f*3.14));
        sphPt[i].angle1=atan(sphPt[i].y/sphPt[i].x)/3.14*180;
        sphPt[i].angle2=asin(sphPt[i].z/7.85)/3.14*180;
        if(sphPt[i].x<0)
        {
            sphPt[i].angle1+=180;
        }
        else if(sphPt[i].y<0)
        {
            sphPt[i].angle1+=360;
        }

        counter+=1.f/NUMPERDEG;
    }

    counter=-27;
    for(int i=3116*NUMPERDEG;i<3170*NUMPERDEG;i++)  //上极 椭圆7  右半边   交叉点还没定义好  斜率没定义。。。。
    {
        sphPt[i].x=7.85*0.1736*cos(counter/180.f*3.14);
        sphPt[i].y=7.85*sin(counter/180.f*3.14);
        sphPt[i].z=fabs(7.85*0.9843*cos(counter/180.f*3.14));
        sphPt[i].angle1=atan(sphPt[i].y/sphPt[i].x)/3.14*180;
        sphPt[i].angle2=asin(sphPt[i].z/7.85)/3.14*180;
        if(sphPt[i].x<0)
        {
            sphPt[i].angle1+=180;
        }
        else if(sphPt[i].y<0)
        {
            sphPt[i].angle1+=360;
        }

        counter+=1.f/NUMPERDEG;
    }

    counter=153;
    for(int i=3170*NUMPERDEG;i<3224*NUMPERDEG;i++)  //上极 椭圆7  左半边   交叉点还没定义好  斜率没定义。。。。
    {
        sphPt[i].x=7.85*0.1736*cos(counter/180.f*3.14);
        sphPt[i].y=7.85*sin(counter/180.f*3.14);
        sphPt[i].z=fabs(7.85*0.9843*cos(counter/180.f*3.14));
        sphPt[i].angle1=atan(sphPt[i].y/sphPt[i].x)/3.14*180;
        sphPt[i].angle2=asin(sphPt[i].z/7.85)/3.14*180;
        if(sphPt[i].x<0)
        {
            sphPt[i].angle1+=180;
        }
        else if(sphPt[i].y<0)
        {
            sphPt[i].angle1+=360;
        }

        counter+=1.f/NUMPERDEG;
    }

    //下极带的点  相当于上极带的点 经度角-9，纬度为负得到。
    for(int i=3224*NUMPERDEG;i<3568*NUMPERDEG;i++)
    {
        sphPt[i].angle1=sphPt[i-344*NUMPERDEG].angle1-9;
        sphPt[i].angle2=-sphPt[i-344*NUMPERDEG].angle2;
        sphPt[i].x=7.85*cos(sphPt[i].angle2/180.f*3.14)*cos(sphPt[i].angle1/180.f*3.14);
        sphPt[i].y=7.85*cos(sphPt[i].angle2/180.f*3.14)*sin(sphPt[i].angle1/180.f*3.14);
        sphPt[i].z=7.85*sin(sphPt[i].angle2/180.f*3.14);

    }
    return 3568*NUMPERDEG;
}
void openglwidget::InitWeldEdgeOfSphere(void)
{
//	memset(sphweld,2,sizeof(SphereWeldseam));
    for(int i=0;i<SPHERECROSSPTNUM;i++)
    {
        for(int j=0;j<SPHERECROSSPTNUM;j++)
        {
            sphweld[i][j].dig=2;
            sphweld[j][i].dig=2;
            sphweld[i][j].visitednum=0;
            sphweld[j][i].visitednum=0;

        }
    }
    ///赤道带与上温带
    for(int i=12;i<92;i++)
    {
        if(i!=31 && i!=71 && i!=91)
        {
            sphweld[i][i+1].dig=0;
            sphweld[i+1][i].dig=0;
        }
    }
    for(int i=12;i<32;i++)
    {
        sphweld[i][2*i+8].dig=1;
        sphweld[2*i+8][i].dig=1;
    }
    for(int i=72;i<92;i++)
    {
        sphweld[i][2*i-111].dig=1;
        sphweld[2*i-111][i].dig=1;
    }
    sphweld[31][12].dig=0;
    sphweld[12][31].dig=0;
    sphweld[71][32].dig=0;
    sphweld[32][71].dig=0;
    sphweld[91][72].dig=0;
    sphweld[72][91].dig=0;
    ////上下极
    for(int i=4;i<8;i++)
    {
        sphweld[i][i+4].dig=1;
        sphweld[i+4][i].dig=1;
        sphweld[i+92][i+96].dig=1;
        sphweld[i+96][i+92].dig=1;
    }
    int i=2,j=6,t=51;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;
    i=2,j=3,t=51;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;
    i=3,j=7,t=51;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;


    i=0,j=4,t=52;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;
    i=0,j=1,t=52;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;
    i=1,j=5,t=52;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;

    i=5,j=6,t=61;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;

    i=4,j=7,t=62;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;

    i=1,j=2,t=71;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;

    i=0,j=3,t=72;
    sphweld[i][j].dig=t;	sphweld[j][i].dig=t;
    sphweld[i+92][j+92].dig=t+2;	sphweld[j+92][i+92].dig=t+2;
}
void openglwidget::InitSphereWeldRoute(void)      //交叉点
{
     //加载路径
     int i=0;
     QFile file("./routeofsphere20181221.txt");
     if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
         while (!file.atEnd())
         {
             QByteArray line = file.readLine();
             QString str(line);
             QStringList strList = str.split(" ");
             sphroute[i].ptnum=strList[0].toUInt();
             sphroute[i].nextptnum=strList[1].toUInt();
             sphroute[i].visitedtime=strList[2].toUInt();
             sphroute[i].dig=strList[3].toUInt();
             i++;
         }
         linesnum=i;
         file.close();
     }

     i=0;
     QFile file1("./crossnode.txt");
     if (file1.open(QIODevice::ReadOnly | QIODevice::Text)){
         while (!file1.atEnd())
         {
             QByteArray line = file1.readLine();
             QString str(line);
             QStringList strList = str.split('\t');
             sphcrossPt[i].ptnum=strList[0].toUInt();
             sphcrossPt[i].angle1=strList[1].toUInt();
             sphcrossPt[i].angle2=strList[2].toUInt();
             sphcrossPt[i].endptnum[0]=strList[3].toUInt();
             sphcrossPt[i].endptnum[1]=strList[4].toUInt();
             sphcrossPt[i].endptnum[2]=strList[5].toUInt();
             i++;
         }
         file1.close();
     }
     ///对于非经线 纬线的焊缝路径 给定 轨迹在 sphPt【】中的位置
     sphroutenum[51].from=2916*NUMPERDEG;	 sphroutenum[51].to=2966*NUMPERDEG;
     sphroutenum[52].from=2966*NUMPERDEG;	 sphroutenum[52].to=3016*NUMPERDEG;
     sphroutenum[61].from=3016*NUMPERDEG;	 sphroutenum[61].to=3066*NUMPERDEG;
     sphroutenum[62].from=3066*NUMPERDEG;	 sphroutenum[62].to=3116*NUMPERDEG;
     sphroutenum[71].from=3116*NUMPERDEG;	 sphroutenum[71].to=3170*NUMPERDEG;
     sphroutenum[72].from=3170*NUMPERDEG;	 sphroutenum[72].to=3224*NUMPERDEG;

     sphroutenum[53].from=3260*NUMPERDEG;	 sphroutenum[53].to=3310*NUMPERDEG;
     sphroutenum[54].from=3310*NUMPERDEG;	 sphroutenum[54].to=3360*NUMPERDEG;
     sphroutenum[63].from=3360*NUMPERDEG;	 sphroutenum[63].to=3410*NUMPERDEG;
     sphroutenum[64].from=3410*NUMPERDEG;	 sphroutenum[64].to=3460*NUMPERDEG;
     sphroutenum[73].from=3460*NUMPERDEG;	 sphroutenum[73].to=3514*NUMPERDEG;
     sphroutenum[74].from=3514*NUMPERDEG;	 sphroutenum[74].to=3568*NUMPERDEG;

}
void openglwidget::DrawWeldLinesOfSphere(void)
{
    glEnable(GL_LINE_SMOOTH);

    glColor3f(0.f,0.f,0.f);
    glLineWidth(5.f);
    for(int i=1;i<numofweldptsp;i++)
    {
        glBegin(GL_LINES);
        glVertex3f(sphPt[i-1].x,sphPt[i-1].y,sphPt[i-1].z);
        glVertex3f(sphPt[i].x,sphPt[i].y,sphPt[i].z);
        glEnd();
    }

}
bool openglwidget::DrawMySphere(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor3f(0.5f, 0.5f, 0.5f);

    angle[0] =MMove.x()/5;
    glRotatef(angle[0], 0.0f, 0.f, 1.f);
    GLUquadricObj *quadratic;
    quadratic=gluNewQuadric();
    glPushMatrix();            //将当前矩阵压入矩阵堆栈
    glTranslatef(0,0,-1.4);   //平移到起点，即A点
    glColor3f(0.5, 0.5, 0.5);
    gluSphere(quadratic,7.85f,50,50); //画球
    drawxyz();
    DrawWeldLinesOfSphere();
    DrawPTofInrobSph();
    glPopMatrix();
    glFlush();
    glLoadIdentity();
    gluLookAt( 0, -30, 0, 0.0, 0.0, 0, 0, 0, 1);
    return true;
}
void openglwidget::mousePressEvent(QMouseEvent* ev){
    if(ev->button() == Qt::LeftButton){
        P_Point = ev->globalPos();
        C_Point = P_Point;
        mouseDown = true;
    }
}
void openglwidget::mouseReleaseEvent(QMouseEvent* ev){
    if(ev->button() == Qt::LeftButton){
        LMove = MMove;
        mouseDown = false;
    }
}
void openglwidget::mouseMoveEvent(QMouseEvent *ev){
    C_Point = ev->globalPos();

}
void openglwidget::dealTimeOut(){
    if(mouseDown){
        MMove.setX(C_Point.x() - P_Point.x()+LMove.x());
        MMove.setY(P_Point.y() - C_Point.y()+LMove.y());
    }
    update();

}
```

---

## 9. openglwidget.ui（OpenGL Widget UI）

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>openglwidget</class>
 <widget class="QWidget" name="openglwidget">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>400</width>
    <height>300</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>Form</string>
  </property>
 </widget>
 <resources/>
 <connections/>
</ui>
```

---

## 统计信息

| 指标 | 数值 |
|------|------|
| 源文件总数 | 9 个（不含 .pro.user 和 cat.png） |
| C++ 源文件 | 3 个（main.cpp, mainwindow.cpp, openglwidget.cpp） |
| C++ 头文件 | 3 个（config.h, mainwindow.h, openglwidget.h） |
| UI 文件 | 2 个（mainwindow.ui, openglwidget.ui） |
| 工程文件 | 1 个（robot_control.pro） |
| 总代码行数（估算） | 约 2500+ 行 |
