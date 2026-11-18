"""Historical first-growth allowance regression for the vote-gated Cell build.

Real mating, vote, editor, and free saved name change. The negative control
runs last because it deliberately corrupts the disposable fixture's allowance.
The enabled runs must preserve the native spent balance. No stage replay.

Use the pre-personal-editor Lineages 29 candidate for this control. The current
personal editor uses the native allowance without waiting for a co-op vote.
"""
import argparse
from pathlib import Path
import time
from lineages_cell_report import Console, command, values, wait_until
from lineages_cell_editor_report import click


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    h,g=Console(4),Console(5)
    for c in (h,g):
        assert 'duoqabudgetfour' in command(c,'coop.savedb').lower()
        wait_until(lambda:'Cell:' in c.call('coop.avatar')['out'],40)
        command(c,'coop.pool invulnfor 600')
    wait_until(lambda:'Player slots: 4,' in command(h,'coop.status'),10)
    wait_until(lambda:values(g,'coop.pool').get('mirrors',0)>5,20)
    food=values(h,'coop.pool dna')['food']
    if food != 50:
        # Negative native progress is not reward-scaled; this is a QA setup only.
        amount=50-food if food>50 else (50-food)*4
        command(h,'coop.pool food '+str(int(amount)))
    wait_until(lambda:values(h,'coop.pool dna')['food']==50 and values(g,'coop.pool dna')['food']==50,10)
    assert values(h,'coop.pool dna')['earned']==5
    print('PASS four-seat first growth: 50 shared food, 5 earned DNA',flush=True)
    try:
        for trial,enabled in enumerate((True,True,False)):
            for c in (h,g):command(c,'coop.pool invulnfor 600')
            command(h,'coop.fix cell.editor_budget '+('on' if enabled else 'off'))
            # A native part-token tutorial may interrupt setup; acknowledge only
            # this observed caption, never force-end an arbitrary cinematic.
            if 'When you see the new part appear' in h.call('coop.ui tree 8')['out']:
                click(h,770,570)
            wait_until(lambda:'cine=0' in command(h,'coop.pool'),25)
            before=values(h,'coop.pool dna')
            command(h,'coop.pool mate button');command(h,'wait.frames 12 3000')
            click(h,400,538)
            wait_until(lambda:'alive=1' in command(h,'coop.pool mate'),12)
            command(h,'coop.pool mate tp')
            wait_until(lambda:'vote active' in command(g,'coop.vote'),30)
            click(g,160,508)
            wait_until(lambda:'Editor' in command(h,'game.mode'),20)
            command(h,'wait.frames 60 7000')
            command(h,'shot "'+(args.output/f'editor-{trial}.png').resolve().as_posix()+'"')
            click(h,400,576);click(h,400,332)
            command(h,'key home');command(h,'wait.frames 4 1000')
            for _ in range(64):command(h,'key del')
            command(h,'wait.frames 4 1000')
            command(h,'text QABudget'+str(trial))
            command(h,'wait.frames 12 3000')
            click(h,778,582);click(h,778,582)
            wait_until(lambda:'Cell:' in h.call('coop.avatar')['out'],25)
            command(h,'coop.pool invulnfor 600')
            after=values(h,'coop.pool dna')
            print('Fix',enabled,'before',before,'after',after,flush=True)
            if enabled:
                assert after['spent']==before['spent'], 'Free save changed spent DNA'
                print('PASS native allowance preserved through free save',flush=True)
            else:
                assert after['spent']<before['spent'], 'Old path did not reproduce extra budget'
                print('EXPECTED FAIL old editor manufactured DNA',flush=True)
            for c in (h,g):assert 'total faults: 0' in command(c,'exc')
    finally:
        command(h,'coop.fix cell.editor_budget on')


if __name__=='__main__':main()
