#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("XXT Min");

    QWidget window;
    window.setWindowTitle("XXT Minimal Test");

    QVBoxLayout *layout = new QVBoxLayout(&window);

    QLabel *label = new QLabel("Hello XXT!\nQt is working.");
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-size: 18px; margin: 20px;");

    QPushButton *btn = new QPushButton("Click Me");
    btn->setStyleSheet("padding: 10px; background: #1976D2; color: white; border-radius: 5px;");

    QObject::connect(btn, &QPushButton::clicked, [&]() {
        label->setText("Button clicked!\nApp is running OK.");
    });

    layout->addWidget(label);
    layout->addWidget(btn);
    layout->addStretch();

    window.resize(360, 640);
    window.show();

    return app.exec();
}
