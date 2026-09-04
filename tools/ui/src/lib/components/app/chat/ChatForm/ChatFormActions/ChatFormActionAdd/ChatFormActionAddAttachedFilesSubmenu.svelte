<script lang="ts">
	import { FolderOpen, Loader2 } from '@lucide/svelte';
	import * as DropdownMenu from '$lib/components/ui/dropdown-menu';
	import { ICON_CLASS_DEFAULT } from '$lib/constants';
	import { getChatFormActionsContext } from '$lib/contexts';
	import { ChatUploadsService } from '$lib/services/chat-uploads.service';
	import type { ChatUploadedFile } from '$lib/types';

	interface ServerFile {
		name: string;
		path: string;
		size: number;
	}

	let files = $state<ServerFile[]>([]);
	let loading = $state(false);
	let error = $state('');

	const chatFormActions = getChatFormActionsContext();

	function mimeFromFilename(name: string): string {
		const ext = name.includes('.') ? name.slice(name.lastIndexOf('.') + 1).toLowerCase() : '';
		const map: Record<string, string> = {
			gif: 'image/gif',
			jpeg: 'image/jpeg',
			jpg: 'image/jpeg',
			md: 'text/markdown',
			mp3: 'audio/mpeg',
			mp4: 'video/mp4',
			pdf: 'application/pdf',
			png: 'image/png',
			svg: 'image/svg+xml',
			txt: 'text/plain',
			wav: 'audio/wav',
			webp: 'image/webp'
		};

		return map[ext] || 'application/octet-stream';
	}

	async function loadFiles() {
		loading = true;
		error = '';

		try {
			files = await ChatUploadsService.listFiles();
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to list files';
			files = [];
		} finally {
			loading = false;
		}
	}

	function selectFile(entry: ServerFile) {
		const uploaded: ChatUploadedFile = {
			file: new File([], entry.name, { type: mimeFromFilename(entry.name) }),
			id: `server-${entry.path}`,
			name: entry.name,
			serverPath: entry.path,
			size: entry.size,
			type: mimeFromFilename(entry.name)
		};

		chatFormActions.onAttachServerFile?.(uploaded);
	}
</script>

<DropdownMenu.Sub onOpenChange={(open) => open && void loadFiles()}>
	<DropdownMenu.SubTrigger class="flex cursor-pointer items-center gap-2">
		<FolderOpen class={ICON_CLASS_DEFAULT} />

		<span>Attached files</span>
	</DropdownMenu.SubTrigger>

	<DropdownMenu.SubContent class="w-72 p-0">
		{#if loading}
			<div class="px-3 py-4 text-center text-sm text-muted-foreground">
				<Loader2 class="mx-auto mb-1 {ICON_CLASS_DEFAULT} animate-spin" />
				Loading…
			</div>
		{:else if error}
			<div class="px-3 py-4 text-center text-sm text-muted-foreground">{error}</div>
		{:else if files.length === 0}
			<div class="px-3 py-4 text-center text-sm text-muted-foreground">
				No files in the attachment folder
			</div>
		{:else}
			<div class="max-h-80 overflow-y-auto p-1">
				{#each files as entry (entry.path)}
					<DropdownMenu.Item
						class="flex cursor-pointer items-center justify-between gap-2"
						onclick={() => selectFile(entry)}
					>
						<span class="min-w-0 truncate" title={entry.path}>{entry.name}</span>
					</DropdownMenu.Item>
				{/each}
			</div>
		{/if}
	</DropdownMenu.SubContent>
</DropdownMenu.Sub>
