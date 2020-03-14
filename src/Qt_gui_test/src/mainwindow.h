﻿#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <qnode.h>
#include <QImage>
#include <QPixmap>
#include <QMutex>
#include <QTime>
#include <QString>

using namespace test_gui;

namespace Ui {
class MainWindow;
}


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(int argc, char** argv, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButtonConnect_clicked();
    void on_pushButtonSend_clicked();
    void updateLogcamera();
    void updateText();
    void displayMat(const QImage& image);
    void displayText(const QString& text);

    void on_pushButton_clicked();

    void on_horizontalSlider_sliderMoved(int position);

    void on_pushButton_5_clicked();

    void on_pushButton_13_clicked();

private:
    Ui::MainWindow *ui;
    QNode qnode;
    QImage qimage_;
    mutable QMutex qimage_mutex_;
};

#endif // MAINWINDOW_H
