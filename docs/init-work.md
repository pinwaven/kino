# init work

- 当前工程是一个带lvgl的esp32-s3 工程， lvgl的demo已经跑起来。

- 现在我需要用esp32的串口(gpio43/44)和一个stm32进行沟通，协议在docs/stm32_esp32_interface.md。
  - 需要实现进程处理串口数据的接收和发送, 并有可能和界面交互

- 界面方面
  - 可以实现几个page
    - 待机界面， 显示当前bms_info， 以及一些状态信息
    - 电机界面， 控制电机的动作, open/ close/ stop按钮, 以及显示当前电机状态
    - 设置界面， 设置wifi、rtc时间等
