/*
 * The touch injector with several contacts at once: id= on tap, hold, swipe
 * and release, the contacts command, and the GT911 frames that carry up to
 * five contacts, each with its own track ID, per report period.  The
 * simulated panel reads every 10ms as the panel does (inj_rig.h).
 *
 * The station's case: a drag under way on one contact while a second taps
 * DISARM or STOP; and a two-finger pinch for the LOGS plot's zoom.
 *
 * SPDX-License-Identifier: MIT
 */

#include "inj_rig.h"

/** @brief Send several lines in one input call, as one USB packet. */
static void send_block(const char *lines)
{
    injector_input(&inj, lines, strlen(lines), now);
    drain();
}

/** @brief The index of the first frame read at or after @p t, or nseen. */
static int frame_at(uint32_t t)
{
    for (int i = 0; i < nseen; ++i) {
        if (seen[i].t >= t) {
            return i;
        }
    }
    return nseen;
}

/* ------------------------------------------------------------- the parser */

TEST_CASE(id_is_parsed_and_checked)
{
    fresh(0);
    send("tap 0 0 id=1");
    CHECK_STR_EQ(last(), "err unconfirmed");
    confirm_ok();
    struct {
        const char *cmd;
        const char *reply;
    } cases[] = {
        { "tap 0 0 id=16", "err range id" },
        { "tap 0 0 id=-1", "err range id" },
        { "tap 0 0 id=99999999999", "err range id" },
        { "tap 1024 0 id=99", "err range id" },
        { "tap 0 0 id=", "err syntax tap" },
        { "tap 0 0 id=x", "err syntax tap" },
        { "tap 0 0 id=1 50", "err syntax tap" },
        { "tap 0 id=1", "err syntax tap" },
        { "tap 1024 0 id=1", "err range x" },
        { "hold 0 0 id=1", "err syntax hold" },
        { "hold 0 0 30001 id=1", "err range ms" },
        { "swipe 0 0 1 1 id=1", "err syntax swipe" },
        { "swipe 0 0 1 1 1 1 id=1", "err syntax swipe" },
        { "release id=16", "err range id" },
        { "release id=", "err syntax release" },
        { "release id=1 now", "err syntax release" },
        { "release now id=1", "err syntax release" },
        { "contacts now", "err syntax contacts" },
        { "contacts", "contacts n=0" },
        { "release id=4", "ok release id=4" },
        { "tap 1 2 id=3", "ok tap id=3" },
        { "tap 1 2 50 id=4", "ok tap id=4" },
        { "hold 1023 599 30000 id=15", "ok hold id=15" },
        { "swipe 0 0 1023 599 1 id=0", "ok swipe id=0" },
        { "tap 5 5 id=3", "err busy" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        send(cases[i].cmd);
        if (strcmp(last(), cases[i].reply) != 0) {
            T_FAIL("%s: got \"%s\", want \"%s\"", cases[i].cmd, last(),
                   cases[i].reply);
        }
    }
}

/* The GT911 has five points: a sixth contact is refused until one lifts,
 * and its lift counts until the panel has read it. */
TEST_CASE(a_sixth_contact_is_refused)
{
    active(0);
    for (int id = 0; id < 5; ++id) {
        char cmd[40];
        snprintf(cmd, sizeof(cmd), "hold %d 300 5000 id=%d", 100 + 150 * id,
                 id);
        send(cmd);
        CHECK(strncmp(last(), "ok hold id=", 11) == 0);
    }
    send("tap 1 1 id=5");
    CHECK_STR_EQ(last(), "err full");
    send("tap 1 1");               /* contact 0 is under way */
    CHECK_STR_EQ(last(), "err busy");
    run(50);
    CHECK_EQ(seen[nseen - 1].n, 5);

    send("release id=2");
    CHECK_STR_EQ(replies[nreplies - 2], "fail hold id=2 released");
    CHECK_STR_EQ(last(), "ok release id=2");
    send("tap 1 1 id=5");          /* the lift is not read yet */
    CHECK_STR_EQ(last(), "err full");
    send("tap 1 1 id=2");
    CHECK_STR_EQ(last(), "err busy");
    run(30);
    CHECK_EQ(seen[nseen - 1].n, 4);
    CHECK(seen_id(&seen[nseen - 1], 2) == NULL);
    send("tap 1 1 id=5");
    CHECK_STR_EQ(last(), "ok tap id=5");
}

TEST_CASE(an_id_is_free_again_after_its_lift)
{
    active(0);
    for (int round = 0; round < 3; ++round) {
        nreplies = 0;
        send("tap 500 300 id=3");
        CHECK_STR_EQ(last(), "ok tap id=3");
        send("hold 10 10 500 id=3");
        CHECK_STR_EQ(last(), "err busy");
        CHECK(run_until("done tap id=3", 400));
        unsigned frames = 0, ms = 0;
        CHECK_EQ(sscanf(reply_with("done tap id=3"),
                        "done tap id=3 frames=%u ms=%u", &frames, &ms), 2);
        CHECK_EQ(frames, 2u);
        CHECK(ms >= 100u && ms <= 110u);
    }
    send("contacts");
    CHECK_STR_EQ(last(), "contacts n=0");
}

/* contacts lists every id in use; status still speaks for contact 0 alone,
 * word for word as protocol 1 did. */
TEST_CASE(contacts_lists_and_status_keeps_to_contact_0)
{
    active(0);
    send("hold 400 240 2000 id=1");
    run(30);
    send("contacts");
    CHECK_STR_EQ(last(), "contacts n=1 1=hold,1,400,240");
    send("status");
    CHECK(strncmp(last(), "status idle ", 12) == 0);
    CHECK(strstr(last(), " down=0 x=0 y=0") != NULL);
    /* Contact 0 idle, contact 1 down: the LED is blue all the same. */
    CHECK_EQ(injector_led(&inj, now), INJ_LED_DOWN);

    send("hold 10 20 2000");
    run(30);
    send("contacts");
    CHECK_STR_EQ(last(), "contacts n=2 0=hold,1,10,20 1=hold,1,400,240");
    send("status");
    CHECK(strstr(last(), "status hold ") == last());
    CHECK(strstr(last(), " down=1 x=10 y=20") != NULL);
}

/* The longest contacts line: five lifts at the far corner, ids 11 to 15. */
TEST_CASE(the_longest_contacts_line_fits)
{
    active(0);
    for (int id = 11; id <= 15; ++id) {
        char cmd[40];
        snprintf(cmd, sizeof(cmd), "hold 1023 599 5000 id=%d", id);
        send(cmd);
    }
    run(30);
    send("release");
    send("contacts");
    const char *want = "contacts n=5 11=release,0,1023,599 12=release,0,1023,599"
                       " 13=release,0,1023,599 14=release,0,1023,599"
                       " 15=release,0,1023,599";
    CHECK_STR_EQ(last(), want);
    CHECK(strlen(want) < INJ_REPLY_LEN);
}

/* -------------------------------------------------------------- the frames */

TEST_CASE(a_frame_carries_five_points)
{
    fresh(0);
    gt911_emu_frame_t f = { .n = 5 };
    for (uint8_t k = 0; k < 5; ++k) {
        f.pt[k] = (gt911_emu_point_t){ .id = (uint8_t)(10 + k),
                                       .x = (uint16_t)(100 * k + 3),
                                       .y = (uint16_t)(500 + k) };
    }
    CHECK(gt911_emu_publish(&inj.emu, &f));
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0x85);
    for (unsigned k = 0; k < 5; ++k) {
        const uint16_t at = (uint16_t)(GT911_EMU_REG_POINT1 + 8u * k);
        CHECK_EQ(gt911_emu_peek(&inj.emu, at), 10 + k);
        CHECK_EQ(gt911_emu_peek(&inj.emu, (uint16_t)(at + 1u))
                     | (gt911_emu_peek(&inj.emu, (uint16_t)(at + 2u)) << 8),
                 100 * k + 3);
        CHECK_EQ(gt911_emu_peek(&inj.emu, (uint16_t)(at + 3u))
                     | (gt911_emu_peek(&inj.emu, (uint16_t)(at + 4u)) << 8),
                 500 + k);
        CHECK_EQ(gt911_emu_peek(&inj.emu, (uint16_t)(at + 5u)), GT911_EMU_SIZE);
    }
    CHECK_EQ(gt911_emu_peek(&inj.emu, 0x8177), 0);
    const uint8_t zero = 0;
    m_write(GT911_EMU_REG_STATUS, &zero, 1);

    /* A smaller frame clears the slots it does not use. */
    f.n = 2;
    CHECK(gt911_emu_publish(&inj.emu, &f));
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0x82);
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_POINT1 + 16u), 0);
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_POINT1 + 39u), 0);
    m_write(GT911_EMU_REG_STATUS, &zero, 1);

    /* A sixth point is refused and leaves the register file as it was. */
    f.n = 6;
    CHECK(!gt911_emu_publish(&inj.emu, &f));
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
}

/*
 * The station's step: a drag on contact 0 (the protocol-1 swipe), and a tap
 * on contact 1 while it runs.  Every frame read while the tap is down holds
 * both contacts; the drag keeps its pace and its 100px step.
 */
TEST_CASE(a_tap_on_a_second_contact_during_a_drag)
{
    active(0);
    send("swipe 100 300 600 300 1000");
    CHECK_STR_EQ(last(), "ok swipe");
    run(300);
    const uint32_t t_tap = now;
    send("tap 900 500 id=1");
    CHECK_STR_EQ(last(), "ok tap id=1");
    CHECK(run_until("done tap id=1", 300));
    unsigned frames = 0, ms = 0;
    CHECK_EQ(sscanf(reply_with("done tap id=1"),
                    "done tap id=1 frames=%u ms=%u", &frames, &ms), 2);
    CHECK_EQ(frames, 2u);
    CHECK(ms >= 100u && ms <= 120u);
    CHECK(run_until("done swipe", 1500));
    CHECK(reply_with("done swipe frames=") != NULL);

    int with_tap = 0;
    bool tap_seen = false, tap_gone = false;
    for (int i = frame_at(t_tap); i + 1 < nseen; ++i) {
        const seen_pt_t *drag = seen_id(&seen[i], 0);
        const seen_pt_t *tap = seen_id(&seen[i], 1);
        CHECK(drag != NULL);
        CHECK_EQ(drag->y, 300);
        if (i > 0) {
            const seen_pt_t *before = seen_id(&seen[i - 1], 0);
            CHECK(before != NULL);
            CHECK(drag->x >= before->x);
            CHECK(drag->x - before->x <= 100);
        }
        if (tap != NULL) {
            CHECK(!tap_gone);       /* down once, then up for good */
            CHECK_EQ(seen[i].n, 2);
            CHECK_EQ(tap->x, 900);
            CHECK_EQ(tap->y, 500);
            tap_seen = true;
            ++with_tap;
        } else if (tap_seen) {
            tap_gone = true;
            CHECK_EQ(seen[i].n, 1);
        }
    }
    CHECK(tap_seen && tap_gone);
    CHECK(with_tap >= 9 && with_tap <= 12);   /* 100ms of 10ms frames */
    CHECK_EQ(seen[nseen - 1].n, 0);
}

/* A hold on one contact ends on time while a swipe on another needs a frame
 * every report period, and the hold's frames= counts its own two. */
TEST_CASE(a_hold_keeps_its_length_beside_a_swipe)
{
    active(0);
    send_block("swipe 100 100 900 500 1000 id=2\nhold 50 550 200 id=4\n");
    CHECK(run_until("done hold id=4", 400));
    unsigned frames = 0, ms = 0;
    CHECK_EQ(sscanf(reply_with("done hold id=4"),
                    "done hold id=4 frames=%u ms=%u", &frames, &ms), 2);
    CHECK_EQ(frames, 2u);
    CHECK(ms >= 200u && ms <= 220u);
    /* On the panel: from the first frame with it to the first without. */
    int first = -1, gone = -1;
    for (int i = 0; i < nseen; ++i) {
        const bool has = seen_id(&seen[i], 4) != NULL;
        if (has && first < 0) {
            first = i;
        }
        if (!has && first >= 0 && gone < 0) {
            gone = i;
        }
    }
    CHECK(first == 0 && gone > 0);
    const uint32_t held = seen[gone].t - seen[first].t;
    if (held < 200u || held > 220u) {
        T_FAIL("held %lu ms", (unsigned long)held);
    }
    CHECK(run_until("done swipe id=2", 1500));
}

/*
 * A two-finger pinch for the LOGS plot: both swipes in one write start in
 * the same frame, every frame moves both, and both lift together.
 */
TEST_CASE(a_pinch_moves_both_contacts_every_frame)
{
    active(0);
    send_block("swipe 400 300 200 300 500 id=1\n"
               "swipe 600 300 800 300 500 id=2\n");
    CHECK(run_until("done swipe id=2", 1000));
    CHECK(reply_with("done swipe id=1 ") != NULL);
    CHECK(nseen >= 50);
    CHECK_EQ(seen[0].n, 2);
    CHECK_EQ(seen_id(&seen[0], 1)->x, 400);
    CHECK_EQ(seen_id(&seen[0], 2)->x, 600);
    int gap = 200;
    for (int i = 0; i + 1 < nseen; ++i) {
        CHECK_EQ(seen[i].n, 2);
        const seen_pt_t *a = seen_id(&seen[i], 1);
        const seen_pt_t *b = seen_id(&seen[i], 2);
        CHECK(a != NULL && b != NULL);
        CHECK_EQ(a->y, 300);
        CHECK_EQ(b->y, 300);
        /* Symmetric about x 500 and opening. */
        CHECK_EQ(a->x + b->x, 1000);
        CHECK(b->x - a->x >= gap);
        gap = b->x - a->x;
    }
    CHECK_EQ(gap, 600);
    CHECK_EQ(seen[nseen - 1].n, 0);
}

/* Five holds of 100ms to 500ms: the frame holds five, then one fewer at
 * each deadline, ids and points intact. */
TEST_CASE(five_contacts_lift_one_by_one)
{
    active(0);
    send_block("hold 100 100 100 id=0\nhold 300 150 200 id=1\n");
    send_block("hold 500 200 300 id=2\nhold 700 250 400 id=3\n");
    send_block("hold 900 300 500 id=4\n");
    CHECK(run_until("done hold id=4", 800));
    for (int id = 0; id < 5; ++id) {
        char want[24];
        snprintf(want, sizeof(want), "done hold id=%d", id);
        const char *line = reply_with(want);
        CHECK(line != NULL);
        unsigned frames = 0, ms = 0, got = 0;
        CHECK_EQ(sscanf(line, "done hold id=%u frames=%u ms=%u", &got,
                        &frames, &ms), 3);
        CHECK_EQ(frames, 2u);
        const unsigned len = 100u * (unsigned)(id + 1);
        if (ms < len || ms > len + 20u) {
            T_FAIL("%s", line);
        }
    }
    CHECK_EQ(seen[0].n, 5);
    int n_prev = 5;
    for (int i = 0; i < nseen; ++i) {
        CHECK(seen[i].n <= n_prev);
        n_prev = seen[i].n;
        for (int k = 0; k < seen[i].n; ++k) {
            const seen_pt_t *p = &seen[i].pt[k];
            CHECK(p->id < 5);
            CHECK_EQ(p->x, 100 + 200 * p->id);
            CHECK_EQ(p->y, 100 + 50 * p->id);
            /* The shorter holds lift first. */
            CHECK(k == 0 || p->id > seen[i].pt[k - 1].id);
            CHECK_EQ(seen[i].pt[k].id, 5 - seen[i].n + k);
        }
    }
    CHECK_EQ(seen[nseen - 1].n, 0);
}

/* ------------------------------------------------------------ releases */

TEST_CASE(release_of_one_id_leaves_the_others_down)
{
    active(0);
    send("hold 50 60 2000");
    send("hold 450 60 2000 id=1");
    run(100);
    CHECK_EQ(seen[nseen - 1].n, 2);
    send("release id=1");
    CHECK_STR_EQ(replies[nreplies - 2], "fail hold id=1 released");
    CHECK_STR_EQ(last(), "ok release id=1");
    send("release id=7");          /* an idle id: nothing to say */
    CHECK_STR_EQ(last(), "ok release id=7");
    run(30);
    CHECK_EQ(seen[nseen - 1].n, 1);
    CHECK_EQ(seen[nseen - 1].pt[0].id, 0);
    CHECK_EQ(injector_led(&inj, now), INJ_LED_FAILED);
    CHECK(run_until("done hold frames=", 2100));
    CHECK(reply_with("done hold id=1") == NULL);
}

TEST_CASE(release_without_id_releases_every_contact)
{
    active(0);
    send("hold 50 60 5000 id=3");
    send("swipe 100 100 900 100 3000");
    send("tap 700 400 3000 id=1");
    run(100);
    CHECK_EQ(seen[nseen - 1].n, 3);
    nreplies = 0;
    send("release");
    CHECK_EQ(nreplies, 4);
    CHECK_STR_EQ(replies[0], "fail swipe released");
    CHECK_STR_EQ(replies[1], "fail tap id=1 released");
    CHECK_STR_EQ(replies[2], "fail hold id=3 released");
    CHECK_STR_EQ(replies[3], "ok release");
    run(40);
    CHECK_EQ(seen[nseen - 1].n, 0);
    CHECK_EQ(nreplies, 4);
    send("contacts");
    CHECK_STR_EQ(last(), "contacts n=0");
}

TEST_CASE(a_closed_port_lifts_every_contact_silently)
{
    active(0);
    send("hold 50 60 5000 id=3");
    send("hold 150 60 5000 id=4");
    run(100);
    nreplies = 0;
    injector_host_closed(&inj);
    run(50);
    CHECK_EQ(nreplies, 0);
    CHECK_EQ(seen[nseen - 1].n, 0);
    CHECK_EQ(seen[nseen - 2].n, 2);
}

/* ------------------------------------------------------------- aborts */

TEST_CASE(a_panel_restart_fails_every_contact)
{
    active(0);
    send("hold 300 300 5000");
    send("swipe 10 10 500 10 5000 id=2");
    run(200);
    nreplies = 0;
    uint8_t id[6];
    m_read(GT911_EMU_REG_PRODUCT, id, 6);
    run(1);
    CHECK_EQ(nreplies, 2);
    CHECK_STR_EQ(replies[0], "fail hold panel_reset");
    CHECK_STR_EQ(replies[1], "fail swipe id=2 panel_reset");
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
    const int before = nseen;
    run(100);
    CHECK_EQ(nseen, before);
    send("tap 1 1 id=2");
    CHECK_STR_EQ(last(), "ok tap id=2");
}

TEST_CASE(a_silent_panel_fails_every_contact)
{
    active(0);
    send("hold 300 300 5000 id=9");
    send("hold 400 300 5000 id=8");
    run(200);
    panel_on = false;
    nreplies = 0;
    run(150);
    CHECK_EQ(nreplies, 2);
    CHECK_STR_EQ(replies[0], "fail hold id=8 no_master");
    CHECK_STR_EQ(replies[1], "fail hold id=9 no_master");
    CHECK_EQ(gt911_emu_peek(&inj.emu, GT911_EMU_REG_STATUS), 0);
    CHECK_EQ(injector_led(&inj, now), INJ_LED_FAILED);
}

/* A gesture that starts on a second id while a frame is out joins the next
 * frame; the first id's timing does not move. */
TEST_CASE(a_contact_started_mid_frame_joins_the_next)
{
    active(0);
    send("hold 10 10 300");
    run(5);                        /* the first frame is out, not yet read */
    CHECK(inj.pending);
    send("hold 20 20 300 id=1");
    run(5);                        /* read: contact 0 alone */
    CHECK_EQ(seen[0].n, 1);
    run(20);
    CHECK_EQ(seen[nseen - 1].n, 2);
    CHECK(run_until("done hold id=1", 500));
    unsigned frames = 0, ms = 0;
    CHECK_EQ(sscanf(reply_with("done hold frames="),
                    "done hold frames=%u ms=%u", &frames, &ms), 2);
    CHECK(ms >= 300u && ms <= 310u);
}

int main(void)
{
    RUN(id_is_parsed_and_checked);
    RUN(a_sixth_contact_is_refused);
    RUN(an_id_is_free_again_after_its_lift);
    RUN(contacts_lists_and_status_keeps_to_contact_0);
    RUN(the_longest_contacts_line_fits);
    RUN(a_frame_carries_five_points);
    RUN(a_tap_on_a_second_contact_during_a_drag);
    RUN(a_hold_keeps_its_length_beside_a_swipe);
    RUN(a_pinch_moves_both_contacts_every_frame);
    RUN(five_contacts_lift_one_by_one);
    RUN(release_of_one_id_leaves_the_others_down);
    RUN(release_without_id_releases_every_contact);
    RUN(a_closed_port_lifts_every_contact_silently);
    RUN(a_panel_restart_fails_every_contact);
    RUN(a_silent_panel_fails_every_contact);
    RUN(a_contact_started_mid_frame_joins_the_next);
    return test_summary("inj_multi");
}
