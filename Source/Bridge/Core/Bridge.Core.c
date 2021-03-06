//
//  Bridge.Core.c
//  GTR
//
//  Created by Mengyu Li on 2019/10/18.
//  Copyright © 2019 limengyu.top. All rights reserved.
//

#include <stdio.h>
#include <stdbool.h>
#include "gtr_core.h"

//-------------------------------------//
extern void swift_log_callback(unsigned int flag, char *message);

//-------------------------------------//
static void gtr_log_message_call_back(unsigned int flag, char *message);

//---------- 初始化
void gtr_init(const char *user_agent, unsigned int cylinder_count) {
    gtr_core_init(user_agent, &gtr_log_message_call_back, cylinder_count);
}
//----------

static void gtr_log_message_call_back(unsigned int flag, char *message) {
    swift_log_callback(flag, message);
}