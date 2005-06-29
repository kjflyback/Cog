#import "TrackingCell.h"

@implementation TrackingCell

- (BOOL)startTrackingAt:(NSPoint)startPoint inView:(NSView *)controlView
{
//	DBLog(@"TRACKING");
	tracking = YES;
	return [super startTrackingAt:startPoint inView:controlView];
	
}

- (void)stopTracking:(NSPoint)lastPoint at:(NSPoint)stopPoint inView:(NSView *)controlView mouseIsUp:(BOOL)flag
{
//	DBLog(@"NOT TRACKING");
	tracking = NO;

	[super stopTracking:lastPoint at:stopPoint inView:controlView mouseIsUp:flag];
}

- (BOOL)tracking
{
	return tracking;
}


@end
